#include "nodeagent_framework.hh"

#include <cassert>
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
#include "nodeagent/core/admission_controller.hh"
#include "nodeagent/core/capabilities.hh"
#include "nodeagent/core/data_dependency_fetcher_router.hh"
#include "nodeagent/core/function_resolver.hh"
#include "nodeagent/core/gateway_client.hh"
#include "nodeagent/core/nodeagent_scheduler_router.hh"
#include "nodeagent/core/nodeagent_tlv_handler.hh"
#include "nodeagent/core/object_cache.hh"
#include "nodeagent/core/run_task_service.hh"
#include "nodeagent/core/state_reporter.hh"
#include "nodeagent/core/task_handler_manager.hh"
#include "strij/extensions/data_dependency_fetcher.hh"
#include "strij/extensions/scheduler.hh"
#include "strij/nodeagent/task_handlers.hh"

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
      config::LoadConfig<config::NodeAgentConfig>(absl::GetFlag(FLAGS_config_file));

  if (!config_result.ok()) {
    LOG_ERROR("Config error: {}", config_result.status().message());
    return 1;
  }

  config::NodeAgentConfig config = config_result.value();

  // The state-snapshot cadence defaults to 10s when not configured.
  if (!config.has_heartbeat_interval()) {
    const int64_t ten_secs{10};
    config.mutable_heartbeat_interval()->set_seconds(ten_secs);
  }

  const event::DispatcherSharedPtr dispatcher = std::make_shared<event::DispatcherImpl>();

  // Build and validate the node capabilities advertisement from config. Runs
  // before --validate_only so that bad pools/reservations/handlers/schedulers
  // fail validation too. The node_id is stable for the lifetime of this process.
  const std::string node_id = GenerateNodeId();
  auto capabilities_result = BuildNodeCapabilities(config, node_id);
  if (!capabilities_result.ok()) {
    LOG_ERROR("Capabilities config error: {}", capabilities_result.status().message());
    return 1;
  }

  const std::shared_ptr<const node::NodeCapabilities> capabilities =
      std::make_shared<node::NodeCapabilities>(std::move(capabilities_result).value());

  // Admission controller tracks per-pool/per-type usage; both the task handlers
  // and the configured schedulers share it.
  const auto admission = std::make_shared<AdmissionControllerImpl>(*capabilities, *dispatcher);

  auto function_resolver = std::make_unique<LocalFunctionResolver>();
  const auto object_cache = std::make_shared<InMemoryObjectCache>();

  // The GatewayClient is the node's only outbound capability: every accepted
  // gateway connection is registered with it (below), and it forwards children
  // upstream over live connections (no dial fallback). It is dependency-free and
  // is handed to scheduler factories through NodeSchedulerDeps, so the bundled
  // "default" scheduler's forward path exists from the moment it is created.
  auto gateway_client = std::make_unique<GatewayClient>();

  // Service construction order resolves the cycle
  //   manager -> (workflow) submitter -> scheduler router -> run-task -> manager
  // by creating the manager empty, building RunTaskService over it, building
  // the scheduler router, and only then populating the manager with handlers.
  // Nothing executes a task before Dispatcher::Run(), so the empty window is
  // never observed by RunTask.
  auto task_handler_manager = std::make_shared<TaskHandlerManager>();
  auto run_task_service = std::make_unique<RunTaskServiceImpl>(task_handler_manager, admission);

  // Build the data dependency fetchers and the scheme router. Runs before the
  // --validate_only short-circuit so that unknown fetcher names and duplicate
  // source schemes fail validation too. An empty list is valid: probes still
  // declare deps, but no prefetching occurs and deps never gate readiness.
  const DataDependencyFetcherDeps fetcher_deps{*dispatcher, *object_cache};
  auto data_dependency_fetchers_result =
      BuildDataDependencyFetchers(config.data_dependency_fetchers(), fetcher_deps);
  if (!data_dependency_fetchers_result.ok()) {
    LOG_ERROR("Data dependency fetcher config error: {}",
              data_dependency_fetchers_result.status().message());
    return 1;
  }
  std::vector<extensions::DataDependencyFetcherPtr> data_dependency_fetchers =
      std::move(data_dependency_fetchers_result).value();

  auto fetcher_router_result =
      DataDependencyFetcherRouter::Build(*object_cache, std::move(data_dependency_fetchers));
  if (!fetcher_router_result.ok()) {
    LOG_ERROR("Data dependency fetcher config error: {}", fetcher_router_result.status().message());
    return 1;
  }
  const DataDependencyFetcherRouterPtr& fetch_router = fetcher_router_result.value();

  // One local scheduler instance per configured scheduler entry, composed into
  // the node-side child router (the submission composite that routes
  // locally-originated children by declared authority). The router also owns
  // the node's frame demux: every accepted connection's handler is a thin seam
  // into it. Failing to start when the list is empty, any name is unknown,
  // or the authority declarations are ambiguous (duplicate non-empty
  // task_type, more than one local_default) is intentional: a misconfigured
  // node must not silently advertise a scheduling protocol.
  const NodeSchedulerDeps scheduler_deps{*dispatcher, *run_task_service, admission, *gateway_client,
                                         *fetch_router};
  auto child_router_result = BuildNodeagentSchedulerRouter(config, scheduler_deps);
  if (!child_router_result.ok()) {
    LOG_ERROR("Scheduler config error: {}", child_router_result.status().message());
    return 1;
  }
  const std::unique_ptr<NodeagentSchedulerRouter>& child_router = child_router_result.value();

  // Build the task handlers last: the router (the ChildTaskSubmitter) already
  // exists, so a workflow handler factory receives a valid submitter. This must
  // run before the --validate_only short-circuit so that unknown handler names
  // fail validation.
  const TaskHandlerDeps handler_deps{*dispatcher, *function_resolver, *child_router};
  const absl::Status handler_load_status =
      task_handler_manager->LoadTaskHandlers(config.task_handlers(), handler_deps);
  if (!handler_load_status.ok()) {
    LOG_ERROR("Task handler config error: {}", handler_load_status.message());
    return 1;
  }
  // Guard the empty-then-populated window: once loading succeeds, a non-empty
  // config must have produced a non-empty manager before anything can run.
  assert(!task_handler_manager->empty() || config.task_handlers().empty());

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
  const utils::SignalMonitor signal_monitor(dispatcher);

  logging::Logger& logger = logging::Logger::GetInstance();
  logger.Run();

  LOG_REGISTER_THREAD();

  // The state reporter broadcasts periodic kNodeState snapshots to every
  // established connection at heartbeat_interval.
  auto state_reporter = std::make_shared<StateReporter>(admission, node_id);
  io::PeriodicTimer state_timer(dispatcher, [state_reporter]() { state_reporter->Broadcast(); });
  state_timer.Start(absl::Seconds(config.heartbeat_interval().seconds()));

  io::TcpListener listener{
      dispatcher, config.tlv_listener().port(),
      [capabilities, router = child_router.get(), state_reporter,
       gateway_client = gateway_client.get()](io::Connection& conn) -> io::ProtocolParserPtr {
        // The gateway dialed us; this outbound link is the node's forward path.
        gateway_client->RegisterConnection(conn.Mailbox());
        auto handler = std::make_unique<NodeagentTlvHandler>(router, capabilities);
        handler->SendAdvertisement(conn);
        state_reporter->AddConnection(conn.Mailbox());
        return std::make_unique<io::TlvParser>(
            [hdl = std::move(handler), &conn](io::TlvFrame frame) -> void {
              hdl->HandleFrame(frame, conn);
            });
      }};

  dispatcher->Run();
  logger.Stop();

  return 0;
}

} // namespace strij::nodeagent