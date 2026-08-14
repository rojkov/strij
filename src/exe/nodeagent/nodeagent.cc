#include <memory>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "core/common/signal_monitor.hh"
#include "core/config/config_loader.hh"
#include "core/event/dispatcher_impl.hh"
#include "core/extensions/factory_context.hh"
#include "core/extensions/function_resolver.hh"
#include "core/io/tcp_listener.hh"
#include "core/io/tlv_frame.hh"
#include "core/io/tlv_parser.hh"
#include "core/logging/log.hh"
#include "core/nodeagent/admission_controller.hh"
#include "core/nodeagent/capabilities.hh"
#include "core/nodeagent/nodeagent_tlv_handler.hh"
#include "core/nodeagent/state_reporter.hh"
#include "core/nodeagent/task_handler_manager.hh"
#include "core/io/periodic_timer.hh"

// Generated protobuf headers
#include "core/config/nodeagent.pb.h"

// NOLINTBEGIN
ABSL_FLAG(std::string, config_file, "nodeagent.yaml", "Path to YAML config file");
ABSL_FLAG(bool, validate_only, false, "Validate config and exit");
ABSL_FLAG(uint32_t, port, 0, "Override TLV listener port (0 = use config)");
ABSL_FLAG(std::string, address, "", "Override TLV listener address (empty = use config)");
ABSL_FLAG(std::string, log_level, "", "Override log level (trace|debug|info|warn|error)");
ABSL_FLAG(std::string, log_format, "", "Override log format (text|json)");
// NOLINTEND

auto main(int argc, char** argv) -> int {
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
    config.mutable_heartbeat_interval()->set_seconds(10);
  }

  const strij::event::DispatcherSharedPtr dispatcher =
      std::make_shared<strij::event::DispatcherImpl>();

  // Build the task handler manager from config. This must run before the
  // --validate_only short-circuit so that unknown handler names fail validation.
  auto function_resolver = std::make_unique<strij::extensions::LocalFunctionResolver>();
  strij::extensions::FactoryContextImpl factory_context(dispatcher, std::move(function_resolver));
  auto manager_result =
      strij::nodeagent::BuildTaskHandlerManager(config.task_handlers(), factory_context);
  if (!manager_result.ok()) {
    LOG_ERROR("Task handler config error: {}", manager_result.status().message());
    return 1;
  }
  const std::shared_ptr<strij::nodeagent::TaskHandlerManager>& task_handler_manager =
      manager_result.value();

  // Build and validate the node capabilities advertisement from config. Runs
  // before --validate_only so that bad pools/reservations/handlers fail
  // validation too. The node_id is stable for the lifetime of this process.
  const std::string node_id = strij::nodeagent::GenerateNodeId();
  auto capabilities_result =
      strij::nodeagent::BuildNodeCapabilities(config, *task_handler_manager, node_id);
  if (!capabilities_result.ok()) {
    LOG_ERROR("Capabilities config error: {}", capabilities_result.status().message());
    return 1;
  }
  const std::shared_ptr<const strij::node::NodeCapabilities> capabilities =
      std::make_shared<strij::node::NodeCapabilities>(std::move(capabilities_result).value());

  if (absl::GetFlag(FLAGS_validate_only)) {
    LOG_INFO("Config validation passed");
    return 0;
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
  const strij::common::SignalMonitor signal_monitor(dispatcher);

  strij::logging::Logger& logger = strij::logging::Logger::GetInstance();
  logger.Run();

  LOG_REGISTER_THREAD();

  // Admission controller tracks per-pool/per-type usage derived from admissions
  // and completions; the state reporter broadcasts periodic kNodeState
  // snapshots to every established connection at heartbeat_interval.
  const auto admission = std::make_shared<strij::nodeagent::AdmissionController>(*capabilities);
  auto state_reporter = std::make_shared<strij::nodeagent::StateReporter>(admission, node_id);
  strij::io::PeriodicTimer state_timer(
      dispatcher, [state_reporter]() { state_reporter->Broadcast(); });
  state_timer.Start(absl::Seconds(config.heartbeat_interval().seconds()));

  strij::io::TcpListener listener{
      dispatcher, config.tlv_listener().port(),
      [task_handler_manager, capabilities, admission, state_reporter](
          strij::io::Connection& conn) -> std::unique_ptr<strij::io::ProtocolParser> {
        auto handler = std::make_unique<strij::nodeagent::NodeagentTlvHandler>(
            task_handler_manager, capabilities, admission);
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
