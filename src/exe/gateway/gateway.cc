#include <memory>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "core/common/signal_monitor.hh"
#include "core/config/config_loader.hh"
#include "core/event/dispatcher_impl.hh"
#include "core/io/echo_result_receiver.hh"
#include "core/io/gateway_http_handler.hh"
#include "core/io/gateway_tlv_handler.hh"
#include "core/io/llhttp_parser.hh"
#include "core/io/node_directory.hh"
#include "core/io/result_receiver_storage.hh"
#include "core/io/tcp_listener.hh"
#include "core/io/tlv_parser.hh"
#include "core/logging/log.hh"

// Generated protobuf headers
#include "gateway.pb.h"

// NOLINTBEGIN
ABSL_FLAG(std::string, config_file, "gateway.yaml", "Path to YAML config file");
ABSL_FLAG(bool, validate_only, false, "Validate config and exit");
ABSL_FLAG(uint32_t, http_port, 0, "Override HTTP listener port (0 = use config)");
ABSL_FLAG(std::string, http_address, "", "Override HTTP listener address (empty = use config)");
ABSL_FLAG(std::string, log_level, "", "Override log level (trace|debug|info|warn|error)");
ABSL_FLAG(std::string, log_format, "", "Override log format (text|json)");
ABSL_FLAG(std::string, node_address, "", "Add node connection address (repeatable)");
// NOLINTEND

auto main(int argc, char** argv) -> int {
  absl::ParseCommandLine(argc, argv);

  // Load configuration
  carrot::config::GatewayConfig config;
  carrot::config::ConfigLoadResult result =
      carrot::config::LoadConfig(absl::GetFlag(FLAGS_config_file), {}, &config);

  if (!result.success) {
    LOG_ERROR("Config error: {} at {}:{}", result.error_message, result.error_file,
              result.error_line);
    return 1;
  }

  // Print warnings
  for (const auto& warning : result.warnings) {
    LOG_WARNING("Config warning: {}", warning);
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
  if (!absl::GetFlag(FLAGS_node_address).empty()) {
    // Note: would need repeated flag support for multiple addresses
    carrot::config::NodeConnection* node = config.add_node_connections();
    node->set_address(absl::GetFlag(FLAGS_node_address));
  }

  carrot::event::DispatcherSharedPtr dispatcher = std::make_shared<carrot::event::DispatcherImpl>();
  carrot::common::SignalMonitor signal_monitor(dispatcher);

  auto& logger = carrot::logging::Logger::GetInstance();
  logger.Run();
  LOG_REGISTER_THREAD();

  // Set log level from config
  // Note: Logger::GetInstance().SetLogLevel(config.logging().level());  // if available

  carrot::io::ResultReceiverStorage storage;

  // Node directory with async connect
  auto connection_factory =
      [&storage](carrot::io::Connection& conn) -> std::unique_ptr<carrot::io::ProtocolParser> {
    auto handler = std::make_unique<carrot::io::GatewayTlvHandler>(storage);
    return std::make_unique<carrot::io::TlvParser>(
        [hdl = std::move(handler), &conn](carrot::io::TlvFrame frame) -> void {
          hdl->HandleFrame(frame, conn);
        });
  };

  // Extract node addresses from config
  std::vector<std::string> node_addresses;
  for (const auto& node : config.node_connections()) {
    node_addresses.push_back(node.address());
  }
  if (node_addresses.empty()) {
    node_addresses.push_back("127.0.0.1:9090"); // default fallback
    LOG_WARNING("No node connections configured, using default: 127.0.0.1:9090");
  }

  carrot::io::NodeDirectory node_directory{dispatcher, node_addresses,
                                           std::move(connection_factory)};
  node_directory.StartConnectAll();

  carrot::io::TcpListener http_listener{
      dispatcher, config.http_listener().port(),
      [&](carrot::io::Connection& conn) -> std::unique_ptr<carrot::io::ProtocolParser> {
        auto handler = std::make_unique<carrot::io::GatewayHttpHandler>(
            node_directory, storage,
            [](carrot::io::Connection& conn) -> std::unique_ptr<carrot::io::ResultReceiver> {
              return std::make_unique<carrot::io::EchoResultReceiver>(conn);
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