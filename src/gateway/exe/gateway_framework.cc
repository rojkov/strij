#include <signal.h>

#include <memory>
#include <string>
#include <utility>

#include "gateway_framework.hh"

#include "gateway/extensions/node_discovery/node_discovery.hh"

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/statusor.h"
#include "common/core/common/signal_monitor.hh"
#include "common/core/config/config_loader.hh"
#include "common/core/event/dispatcher_impl.hh"
#include "common/extensions/extension_registry.hh"
#include "gateway/core/gateway_http_handler.hh"
#include "gateway/core/gateway_tlv_handler.hh"
#include "gateway/core/http_result_receiver.hh"
#include "gateway/core/node_directory.hh"
#include "gateway/core/result_receiver_storage.hh"
#include "common/core/io/connection.hh"
#include "common/core/io/llhttp_parser.hh"
#include "common/core/io/protocol_parser.hh"
#include "common/core/io/tcp_listener.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/io/tlv_parser.hh"
#include "common/core/logging/log.hh"
#include "common/core/logging/logger.hh"
#include "gateway/core/scheduler_router/scheduler_router.hh"
#include "gateway_factory_context.hh"
#include "strij/event/dispatcher.hh"

// Generated protobuf headers
#include "gateway/config/gateway.pb.h"
#include "google/protobuf/any.pb.h"

// NOLINTBEGIN
ABSL_FLAG(std::string, config_file, "gateway.yaml", "Path to YAML config file");
ABSL_FLAG(bool, validate_only, false, "Validate config and exit");
ABSL_FLAG(uint32_t, http_port, 0, "Override HTTP listener port (0 = use config)");
ABSL_FLAG(std::string, http_address, "", "Override HTTP listener address (empty = use config)");
ABSL_FLAG(std::string, log_level, "", "Override log level (trace|debug|info|warn|error)");
ABSL_FLAG(std::string, log_format, "", "Override log format (text|json)");
// NOLINTEND

namespace strij::gateway {

auto RunGateway(int argc, char** argv) -> int {
  // Suppress SIGPIPE. Within a single io_uring CQE batch a write SQE can be
  // queued (e.g. delivering a task result to an HTTP connection) and then the
  // target fd closed (HTTP EOF in the same batch). The stale SQE is submitted
  // on the next io_uring_submit_and_wait, causing the kernel to deliver
  // SIGPIPE. The write completion handler already treats <= 0 as an error and
  // tears down the connection. A proper fix is to defer SQE submission until
  // after all CQEs in the batch are drained (see ROADMAP.md).
  signal(SIGPIPE, SIG_IGN);
  absl::ParseCommandLine(argc, argv);

  // Load configuration
  auto config_result =
      strij::config::LoadConfig<strij::config::GatewayConfig>(absl::GetFlag(FLAGS_config_file));

  if (!config_result.ok()) {
    LOG_ERROR("Config error: {}", config_result.status().message());
    return 1;
  }

  strij::config::GatewayConfig config = config_result.value();

  // Validate node_discovery extension is configured
  if (!config.has_node_discovery()) {
    LOG_ERROR("Config error: node_discovery extension is required. "
              "Add a 'node_discovery' section to your config file, e.g.:\n"
              "  node_discovery:\n"
              "    name: \"static\"\n"
              "    typed_config:\n"
              "      \"@type\": \"type.googleapis.com/strij.config."
              "StaticNodeDiscoveryConfig\"\n"
              "      addresses: [\"127.0.0.1:9090\"]");
    return 1;
  }

  // Apply CLI overrides
  if (absl::GetFlag(FLAGS_http_port) != 0) {
    config.mutable_http_listener()->set_port(absl::GetFlag(FLAGS_http_port));
  }
  if (!absl::GetFlag(FLAGS_http_address).empty()) {
    config.mutable_http_listener()->set_address(absl::GetFlag(FLAGS_http_address));
  }
  if (!absl::GetFlag(FLAGS_log_level).empty()) {
    config.mutable_logging()->set_level(absl::GetFlag(FLAGS_log_level));
  }
  if (!absl::GetFlag(FLAGS_log_format).empty()) {
    config.mutable_logging()->set_format(absl::GetFlag(FLAGS_log_format));
  }

  const strij::event::DispatcherSharedPtr dispatcher =
      std::make_shared<strij::event::DispatcherImpl>();
  const strij::utils::SignalMonitor signal_monitor(dispatcher);

  strij::logging::Logger& logger = strij::logging::Logger::GetInstance();
  logger.Run();
  LOG_REGISTER_THREAD();

  // Set log level from config
  // Note: Logger::GetInstance().SetLogLevel(config.logging().level());  // if available

  // TODO: check why so many components are aware of state_tracker. I assumed it's needed for
  // centralized schedulers only.
  strij::gateway::ExactStateTracker state_tracker;
  strij::gateway::ResultReceiverStorageImpl storage{&state_tracker};

  // The connection factory needs the NodeDirectory and the scheduler router,
  // both of which are constructed after it, so back-pointers are filled in
  // once the objects exist. Connections are only accepted during Run(), by
  // which time both pointers are set.
  strij::gateway::NodeDirectoryImpl* node_directory_ptr = nullptr;
  strij::gateway::SchedulerRouter* scheduler_router_ptr = nullptr;
  auto connection_factory = [&storage, &state_tracker, &node_directory_ptr, &scheduler_router_ptr](
                                strij::io::Connection& conn) -> strij::io::ProtocolParserPtr {
    auto handler = std::make_unique<strij::gateway::GatewayTlvHandler>(
        *node_directory_ptr, storage, scheduler_router_ptr, &state_tracker);
    // Move the handler into the parser's callback via a named capture.
    return std::make_unique<strij::io::TlvParser>(
        [hdl = std::move(handler), &conn](strij::io::TlvFrame frame) -> void {
          const absl::Status status = hdl->HandleFrame(frame, conn);
          if (!status.ok()) {
            LOG_WARNING("frame dropped: {}", status.message());
          }
        });
  };

  strij::gateway::NodeDirectoryImpl node_directory{dispatcher, std::move(connection_factory), storage};
  node_directory_ptr = &node_directory;

  strij::gateway::GatewayFactoryContextImpl factory_context(dispatcher, node_directory, storage);

  // Node discovery via extension registry.
  std::unique_ptr<strij::gateway::NodeDiscovery> node_discovery;
  const auto& ext = config.node_discovery();
  auto* factory =
      strij::extensions::Registry<strij::gateway::NodeDiscoveryFactory>::instance().GetFactory(
          ext.name());
  if (factory == nullptr) {
    LOG_ERROR("Node discovery extension '{}' not found. "
              "Ensure the extension library is linked and the name matches a "
              "registered factory.",
              ext.name());
    return 1;
  }

  ::google::protobuf::Any unpacked;
  unpacked.CopyFrom(ext.typed_config());
  auto config_msg = factory->CreateEmptyConfigProto();
  if (!unpacked.UnpackTo(config_msg.get())) {
    LOG_ERROR("Failed to unpack typed_config for extension '{}': unknown type '{}'", ext.name(),
              unpacked.type_url());
    return 1;
  }

  node_discovery = factory->Create(*config_msg, factory_context);
  LOG_INFO("Node discovery extension '{}' loaded", ext.name());

  // Schedulers via extension registry: required (at least one), no silent
  // default. The router validates names and task_type bindings, so an unknown
  // scheduler or an empty list fails startup (and --validate_only).
  auto router_result = strij::gateway::BuildSchedulerRouter(config, factory_context);
  if (!router_result.ok()) {
    LOG_ERROR("Config error: {}", router_result.status().message());
    return 1;
  }
  auto scheduler_router = std::move(router_result).value();
  scheduler_router_ptr = scheduler_router.get();
  LOG_INFO("Loaded {} scheduler(s); requires protocol '{}'", config.schedulers().size(),
           scheduler_router->RequiredProtocol());

  if (absl::GetFlag(FLAGS_validate_only)) {
    LOG_INFO("Config validation passed");
    return 0;
  }

  node_discovery->Start(
      [&node_directory](const std::vector<strij::gateway::NodeInfo>& nodes) -> void {
        node_directory.Reconcile(nodes);
      });

  const strij::io::TcpListener http_listener{
      dispatcher, config.http_listener().port(),
      [&](strij::io::Connection& conn) -> std::unique_ptr<strij::io::ProtocolParser> {
        auto handler = std::make_unique<strij::gateway::GatewayHttpHandler>(
            storage,
            [](strij::io::Connection& conn) -> strij::gateway::ResultReceiverPtr {
              return std::make_unique<strij::gateway::HttpResultReceiver>(conn);
            },
            *scheduler_router);
        return std::make_unique<strij::io::LlhttpParser>(
            [hdl = std::move(handler), &conn](const strij::io::HttpRequest& request) -> void {
              hdl->HandleMessage(request, conn);
            });
      }};

  dispatcher->Run();
  logger.Stop();

  return 0;
}

} // namespace strij::gateway