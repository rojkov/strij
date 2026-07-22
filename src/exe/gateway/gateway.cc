#include <memory>
#include <vector>

#include "src/core/io/protocol_parser.hh"

#include "core/common/signal_monitor.hh"
#include "core/event/dispatcher_impl.hh"
#include "core/io/echo_result_receiver.hh"
#include "core/io/gateway_http_handler.hh"
#include "core/io/gateway_tlv_handler.hh"
#include "core/io/llhttp_parser.hh"
#include "core/io/result_receiver_storage.hh"
#include "core/io/tcp_connector.hh"
#include "core/io/tcp_listener.hh"
#include "core/io/tlv_parser.hh"
#include "core/logging/log.hh"

auto main() -> int {
  carrot::event::DispatcherSharedPtr dispatcher = std::make_shared<carrot::event::DispatcherImpl>();
  carrot::common::SignalMonitor signal_monitor(dispatcher);

  auto& logger = carrot::logging::Logger::GetInstance();
  logger.Run();
  LOG_REGISTER_THREAD();

  carrot::io::ResultReceiverStorage storage;

  // Connect to nodeagents
  carrot::io::TcpConnector connector{dispatcher, storage};
  std::vector<carrot::io::Connection*> nodeagent_conns;

  try {
    auto* conn = connector.Connect("127.0.0.1", 9090);
    nodeagent_conns.push_back(conn);
  } catch (const std::exception& e) {
    LOG_WARNING("Failed to connect to nodeagent: {}", e.what());
  }

  carrot::io::TcpListener http_listener{
      dispatcher, 8081,
      [&](carrot::io::Connection& conn) -> std::unique_ptr<carrot::io::ProtocolParser> {
        auto handler = std::make_unique<carrot::io::GatewayHttpHandler>(
            nodeagent_conns, storage,
            [](carrot::io::Connection& conn) -> std::unique_ptr<carrot::io::ResultReceiver> {
              return std::make_unique<carrot::io::EchoResultReceiver>(conn);
            });
        return std::make_unique<carrot::io::LlhttpParser>(
            [h = std::move(handler), &conn](std::span<const std::byte> msg) {
              h->HandleMessage(msg, conn);
            });
      }};

  dispatcher->Run();
  logger.Stop();

  return 0;
}
