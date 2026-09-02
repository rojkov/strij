#include "nodeagent_framework.hh"

#include <memory>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/statusor.h"
#include "common/core/common/signal_monitor.hh"
#include "common/core/config/config_loader.hh"
#include "common/core/event/dispatcher_impl.hh"
#include "common/core/io/periodic_timer.hh"
#include "common/core/io/tcp_listener.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/io/tlv_parser.hh"
#include "common/core/logging/log.hh"
#include "common/extensions/scheduler_loader.hh"
#include "nodeagent/core/admission_controller.hh"
#include "nodeagent/core/capabilities.hh"
#include "nodeagent/core/function_resolver.hh"
#include "nodeagent/core/nodeagent_tlv_handler.hh"
#include "nodeagent/core/run_task_service.hh"
#include "nodeagent/core/state_reporter.hh"
#include "nodeagent/core/task_handler_manager.hh"
#include "nodeagent_factory_context.hh"
#include "strij/extensions/scheduler.hh"

// Generated protobuf headers
#include "nodeagent/config/nodeagent.pb.h"

// NOLINTBEGIN
ABSL_FLAG(std::string, config_file, "nodeagent.yaml", "Path to YAML config file");
ABSL_FLAG(bool, validate_only, false, "Validate config and exit");
ABSL_FLAG(uint32_t, port, 0, "Override TLV listener port (0 = use config)");
ABSL_FLAG(std::string, address, "", "Override TLV listener address (empty = use config)");
ABSL_FLAG(std::string, log_level, "", "Override log level (trace|debug|info|warn|error)");
ABSL_FLAG(std::string, log_format, "", "Override log format (text|json)");
// NOLINTEND

namespace strij::nodeagent {

auto RunNodeagent(int argc, char** argv) -> int {
  absl::ParseCommandLine(argc, argv);

  // Load configuration
  auto config_result =
      strij::config::LoadConfig<strij::config::NodeAgentConfig>(absl::GetFlag(FLAGS_config_file));

  if (!config_result.ok()) {
    LOG_ERROR("Config error: {}", config_result.status().message());
    return 1;
  }

  strij::config::NodeAgentConfig config = config_result.value();

  // The state-snapshot cadence defaults to 10s when not configured.
  if (!config.has_heartbeat_interval()) {
    const int64_t ten_secs{10};
    config.mutable_heartbeat_interval()->set_seconds(ten_secs);
  }

  const strij::event::DispatcherSharedPtr dispatcher =
      std::make_shared<strij::event::DispatcherImpl>();

  // Build and validate the node capabilities advertisement from config. Runs
  // before --validate_only so that bad pools/reservations/handlers/schedulers
  // fail validation too. The node_id is stable for the lifetime of this process.
  const std::string node_id = strij::nodeagent::GenerateNodeId();
  auto capabilities_result = strij::nodeagent::BuildNodeCapabilities(config, node_id);
  if (!capabilities_result.ok()) {
    LOG_ERROR("Capabilities config error: {}", capabilities_result.status().message());
    return 1;
  }

  const std::shared_ptr<const strij::node::NodeCapabilities> capabilities =
      std::make_shared<strij::node::NodeCapabilities>(std::move(capabilities_result).value());

  // Admission controller tracks per-pool/per-type usage; both the task handlers
  // and (via the factory context) the configured schedulers share it.
  const auto admission = std::make_shared<strij::nodeagent::AdmissionControllerImpl>(*capabilities);

  auto function_resolver = std::make_unique<strij::nodeagent::LocalFunctionResolver>();
  strij::nodeagent::NodeagentFactoryContextImpl factory_context(
      dispatcher, std::move(function_resolver), admission);

  // Build the task handler manager from config. This must run before the
  // --validate_only short-circuit so that unknown handler names fail validation.
  auto manager_result =
      strij::nodeagent::BuildTaskHandlerManager(config.task_handlers(), factory_context);
  if (!manager_result.ok()) {
    LOG_ERROR("Task handler config error: {}", manager_result.status().message());
    return 1;
  }

  if (absl::GetFlag(FLAGS_validate_only)) {
    LOG_INFO("Config validation passed");
    return 0;
  }

  const std::shared_ptr<strij::nodeagent::TaskHandlerManager>& task_handler_manager =
      manager_result.value();

  // The RunTask service is the schedulers' only route to task execution; it is
  // installed into the factory context two-phase so that scheduler factories
  // created below can reach it.
  auto run_task_service =
      std::make_unique<strij::nodeagent::RunTaskServiceImpl>(task_handler_manager, admission);
  factory_context.SetRunTaskService(*run_task_service);

  // One local scheduler instance per configured scheduler entry, shared by
  // every accepted connection's frame dispatcher. Failing to start when the
  // list is empty or any name is unknown is intentional: a misconfigured node
  // must not silently advertise a scheduling protocol.
  std::vector<strij::extensions::SchedulerPtr> local_schedulers;
  std::vector<strij::extensions::Scheduler*> scheduler_pointers;
  scheduler_pointers.reserve(static_cast<size_t>(config.schedulers().size()));
  local_schedulers.reserve(static_cast<size_t>(config.schedulers().size()));
  for (const auto& ext : config.schedulers()) {
    auto scheduler_result = strij::nodeagent::CreateNodeScheduler(ext, factory_context);
    if (!scheduler_result.ok()) {
      LOG_ERROR("Scheduler config error: {}", scheduler_result.status().message());
      return 1;
    }
    scheduler_pointers.push_back(scheduler_result.value().get());
    local_schedulers.push_back(std::move(scheduler_result).value());
  }
  if (local_schedulers.empty()) {
    LOG_ERROR("No schedulers configured for this node");
    return 1;
  }

  // Apply CLI overrides
  if (absl::GetFlag(FLAGS_port) != 0) {
    config.mutable_tlv_listener()->set_port(absl::GetFlag(FLAGS_port));
  }
  if (!absl::GetFlag(FLAGS_address).empty()) {
    config.mutable_tlv_listener()->set_address(absl::GetFlag(FLAGS_address));
  }
  if (!absl::GetFlag(FLAGS_log_level).empty()) {
    config.mutable_logging()->set_level(absl::GetFlag(FLAGS_log_level));
  }
  if (!absl::GetFlag(FLAGS_log_format).empty()) {
    config.mutable_logging()->set_format(absl::GetFlag(FLAGS_log_format));
  }

  // Signal monitor must be activated before the logger thread, otherwise we might miss the signal.
  const strij::utils::SignalMonitor signal_monitor(dispatcher);

  strij::logging::Logger& logger = strij::logging::Logger::GetInstance();
  logger.Run();

  LOG_REGISTER_THREAD();

  // The state reporter broadcasts periodic kNodeState snapshots to every
  // established connection at heartbeat_interval.
  auto state_reporter = std::make_shared<strij::nodeagent::StateReporter>(admission, node_id);
  strij::io::PeriodicTimer state_timer(dispatcher,
                                       [state_reporter]() { state_reporter->Broadcast(); });
  state_timer.Start(absl::Seconds(config.heartbeat_interval().seconds()));

  strij::io::TcpListener listener{
      dispatcher, config.tlv_listener().port(),
      [capabilities, &scheduler_pointers,
       state_reporter](strij::io::Connection& conn) -> std::unique_ptr<strij::io::ProtocolParser> {
        auto handler = std::make_unique<strij::nodeagent::NodeagentTlvHandler>(scheduler_pointers,
                                                                               capabilities);
        handler->SendAdvertisement(conn);
        state_reporter->AddConnection(conn.Mailbox());
        return std::make_unique<strij::io::TlvParser>(
            [hdl = std::move(handler), &conn](strij::io::TlvFrame frame) -> void {
              hdl->HandleFrame(frame, conn);
            });
      }};

  dispatcher->Run();
  logger.Stop();

  return 0;
}

} // namespace strij::nodeagent