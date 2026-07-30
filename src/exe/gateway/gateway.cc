#include <memory>
#include <string>
#include <vector>

#include "src/extensions/node_discovery/node_discovery.hh"

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "core/common/signal_monitor.hh"
#include "core/config/config_loader.hh"
#include "core/event/dispatcher_impl.hh"
#include "core/extensions/extension_registry.hh"
#include "core/extensions/factory_context.hh"
#include "core/gateway/http_result_receiver.hh"
#include "core/gateway/gateway_http_handler.hh"
#include "core/gateway/gateway_tlv_handler.hh"
#include "core/gateway/node_directory.hh"
#include "core/gateway/result_receiver_storage.hh"
#include "core/io/llhttp_parser.hh"
#include "core/io/tcp_listener.hh"
#include "core/io/tlv_parser.hh"
#include "core/logging/log.hh"

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
      carrot::config::LoadConfig<carrot::config::GatewayConfig>(absl::GetFlag(FLAGS_config_file));

  if (!config_result.ok()) {
    LOG_ERROR("Config error: {}", config_result.status().message());
    return 1;
  }

  carrot::config::GatewayConfig config = std::move(config_result).value();

  // Validate node_discovery extension is configured
  if (!config.has_node_discovery()) {
    LOG_ERROR("Config error: node_discovery extension is required. "
              "Add a 'node_discovery' section to your config file, e.g.:\n"
              "  node_discovery:\n"
              "    name: \"static\"\n"
              "    typed_config:\n"
              "      \"@type\": \"type.googleapis.com/carrot.config."
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

  carrot::event::DispatcherSharedPtr dispatcher = std::make_shared<carrot::event::DispatcherImpl>();
  carrot::common::SignalMonitor signal_monitor(dispatcher);

  auto& logger = carrot::logging::Logger::GetInstance();
  logger.Run();
  LOG_REGISTER_THREAD();

  // Set log level from config
  // Note: Logger::GetInstance().SetLogLevel(config.logging().level());  // if available

  carrot::gateway::ResultReceiverStorage storage;

  // Node discovery via extension registry
  carrot::extensions::GatewayFactoryContext factory_context(dispatcher);
  std::unique_ptr<carrot::extensions::NodeDiscovery> node_discovery;

  const auto& ext = config.node_discovery();
  auto* factory =
      carrot::extensions::Registry<carrot::extensions::NodeDiscoveryFactory>::instance().GetFactory(
          ext.name());
  if (!factory) {
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

  // Node directory with async connect
  auto connection_factory =
      [&storage](carrot::io::Connection& conn) -> std::unique_ptr<carrot::io::ProtocolParser> {
    auto handler = std::make_unique<carrot::gateway::GatewayTlvHandler>(storage);
    return std::make_unique<carrot::io::TlvParser>(
        [hdl = std::move(handler), &conn](carrot::io::TlvFrame frame) -> void {
          hdl->HandleFrame(frame, conn);
        });
  };

  std::vector<std::string> node_addresses;
  node_discovery->Start([&node_addresses](std::vector<carrot::extensions::NodeInfo> nodes) {
    for (auto& node : nodes) {
      node_addresses.push_back(std::move(node.address));
    }
  });

  carrot::gateway::NodeDirectory node_directory{dispatcher, node_addresses,
                                           std::move(connection_factory)};
  node_directory.StartConnectAll();

  carrot::io::TcpListener http_listener{
      dispatcher, config.http_listener().port(),
      [&](carrot::io::Connection& conn) -> std::unique_ptr<carrot::io::ProtocolParser> {
        auto handler = std::make_unique<carrot::gateway::GatewayHttpHandler>(
            node_directory, storage,
            [](carrot::io::Connection& conn) -> std::unique_ptr<carrot::gateway::ResultReceiver> {
              return std::make_unique<carrot::gateway::HttpResultReceiver>(conn);
            });
        return std::make_unique<carrot::io::LlhttpParser>(
            [hdl = std::move(handler), &conn](std::span<const std::byte> msg) -> void {
              hdl->HandleMessage(msg, conn);
            });
      }};

  dispatcher->Run();
  logger.Stop();

  return 0;
}