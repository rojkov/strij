#include <memory>
#include <vector>

#include "src/core/io/protocol_parser.hh"

#include "core/common/signal_monitor.hh"
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

auto main() -> int {
  carrot::event::DispatcherSharedPtr dispatcher = std::make_shared<carrot::event::DispatcherImpl>();
  carrot::common::SignalMonitor signal_monitor(dispatcher);

  auto& logger = carrot::logging::Logger::GetInstance();
  logger.Run();
  LOG_REGISTER_THREAD();

  carrot::io::ResultReceiverStorage storage;

  // Node directory with async connect
  auto connection_factory =
      [&storage](carrot::io::Connection& conn) -> std::unique_ptr<carrot::io::ProtocolParser> {
    auto handler = std::make_unique<carrot::io::GatewayTlvHandler>(storage);
    return std::make_unique<carrot::io::TlvParser>(
        [h = std::move(handler), &conn](carrot::io::TlvFrame frame) {
          h->HandleFrame(frame, conn);
        });
  };

  carrot::io::NodeDirectory node_directory{dispatcher, {"127.0.0.1:9090"},
                                           std::move(connection_factory)};
  node_directory.StartConnectAll();

  carrot::io::TcpListener http_listener{
      dispatcher, 8081,
      [&](carrot::io::Connection& conn) -> std::unique_ptr<carrot::io::ProtocolParser> {
        auto handler = std::make_unique<carrot::io::GatewayHttpHandler>(
            node_directory, storage,
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
