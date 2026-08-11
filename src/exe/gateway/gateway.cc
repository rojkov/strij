#include <memory>
#include <string>
#include <utility>

#include "src/extensions/node_discovery/node_discovery.hh"

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "core/common/signal_monitor.hh"
#include "core/config/config_loader.hh"
#include "core/event/dispatcher_impl.hh"
#include "core/extensions/extension_registry.hh"
#include "core/extensions/factory_context.hh"
#include "core/gateway/gateway_http_handler.hh"
#include "core/gateway/gateway_tlv_handler.hh"
#include "core/gateway/http_result_receiver.hh"
#include "core/gateway/node_directory.hh"
#include "core/gateway/result_receiver_storage.hh"
#include "core/io/connection.hh"
#include "core/io/llhttp_parser.hh"
#include "core/io/protocol_parser.hh"
#include "core/io/tcp_listener.hh"
#include "core/io/tlv_frame.hh"
#include "core/io/tlv_parser.hh"
#include "core/logging/log.hh"
#include "core/logging/logger.hh"
#include "extensions/schedulers/scheduler.hh"
#include "strij/event/dispatcher.hh"

// Generated protobuf headers
#include "core/config/gateway.pb.h"
#include "google/protobuf/any.pb.h"

// NOLINTBEGIN
ABSL_FLAG(std::string, config_file, "gateway.yaml", "Path to YAML config file");
ABSL_FLAG(bool, validate_only, false, "Validate config and exit");
ABSL_FLAG(uint32_t, http_port, 0, "Override HTTP listener port (0 = use config)");
ABSL_FLAG(std::string, http_address, "", "Override HTTP listener address (empty = use config)");
ABSL_FLAG(std::string, log_level, "", "Override log level (trace|debug|info|warn|error)");
ABSL_FLAG(std::string, log_format, "", "Override log format (text|json)");
// NOLINTEND

auto main(int argc, char** argv) -> int {
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

  if (absl::GetFlag(FLAGS_validate_only)) {
    LOG_INFO("Config validation passed");
    return 0;
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
  const strij::common::SignalMonitor signal_monitor(dispatcher);

  strij::logging::Logger& logger = strij::logging::Logger::GetInstance();
  logger.Run();
  LOG_REGISTER_THREAD();

  // Set log level from config
  // Note: Logger::GetInstance().SetLogLevel(config.logging().level());  // if available

  strij::gateway::ResultReceiverStorage storage;
  strij::gateway::ExactStateTracker state_tracker;

  // Node discovery via extension registry
  strij::extensions::FactoryContextImpl factory_context(dispatcher);
  std::unique_ptr<strij::extensions::NodeDiscovery> node_discovery;

  const auto& ext = config.node_discovery();
  auto* factory =
      strij::extensions::Registry<strij::extensions::NodeDiscoveryFactory>::instance().GetFactory(
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

  // Scheduler via extension registry: required, no silent default.
  const strij::config::ExtensionConfig* scheduler_config =
      config.has_scheduler() ? &config.scheduler() : nullptr;
  auto scheduler_result = strij::extensions::CreateScheduler(scheduler_config, factory_context);
  if (!scheduler_result.ok()) {
    LOG_ERROR("Config error: {}", scheduler_result.status().message());
    return 1;
  }
  auto scheduler = std::move(scheduler_result).value();
  LOG_INFO("Scheduler '{}' loaded (requires protocol '{}')", config.scheduler().name(),
           scheduler->RequiredProtocol());

  // Node directory with async connect. Nodes are discovered dynamically and
  // reconciled into the directory by repeated discovery snapshots. The
  // connection factory needs the directory (to store advertisements and rekey
  // records), so a back-pointer is filled in after construction.
  strij::gateway::NodeDirectory* node_directory_ptr = nullptr;
  auto connection_factory =
      [&storage, &state_tracker,
       &node_directory_ptr](strij::io::Connection& conn) -> std::unique_ptr<strij::io::ProtocolParser> {
    auto handler = std::make_unique<strij::gateway::GatewayTlvHandler>(*node_directory_ptr, storage,
                                                                       &state_tracker);
    // Move the handler into the parser's callback via a named capture.
    return std::make_unique<strij::io::TlvParser>(
        [hdl = std::move(handler), &conn](strij::io::TlvFrame frame) -> void {
          hdl->HandleFrame(frame, conn);
        });
  };

  strij::gateway::NodeDirectory node_directory{dispatcher, std::move(connection_factory)};
  node_directory_ptr = &node_directory;

  node_discovery->Start(
      [&node_directory](const std::vector<strij::extensions::NodeInfo>& nodes) -> void {
        node_directory.Reconcile(nodes);
      });

  const strij::io::TcpListener http_listener{
      dispatcher, config.http_listener().port(),
      [&](strij::io::Connection& conn) -> std::unique_ptr<strij::io::ProtocolParser> {
        auto handler = std::make_unique<strij::gateway::GatewayHttpHandler>(
            node_directory, storage,
            [](strij::io::Connection& conn) -> strij::gateway::ResultReceiverPtr {
              return std::make_unique<strij::gateway::HttpResultReceiver>(conn);
            },
            *scheduler, &state_tracker);
        return std::make_unique<strij::io::LlhttpParser>(
            [hdl = std::move(handler), &conn](const strij::io::HttpRequest& request) -> void {
              hdl->HandleMessage(request, conn);
            });
      }};

  dispatcher->Run();
  logger.Stop();

  return 0;
}