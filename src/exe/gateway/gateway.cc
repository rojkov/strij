#include "src/core/io/message_handler.hh"
#include "src/core/io/protocol_parser.hh"

#include "core/common/signal_monitor.hh"
#include "core/event/dispatcher_impl.hh"
#include "core/io/http_echo_handler.hh"
#include "core/io/llhttp_parser.hh"
#include "core/io/tcp_listener.hh"
#include "core/logging/log.hh"

auto main() -> int {
  carrot::event::DispatcherSharedPtr dispatcher = std::make_shared<carrot::event::DispatcherImpl>();
  // Signal monitor must be activated before the logger thread, otherwise we might miss the signal.
  carrot::common::SignalMonitor signal_monitor(dispatcher);

  auto& logger = carrot::logging::Logger::GetInstance();
  logger.Run();

  LOG_REGISTER_THREAD();

  carrot::io::TcpListener listener{
      dispatcher, 8081,
      [](std::function<void(std::span<const std::byte>)> on_message)
          -> std::pair<carrot::io::ProtocolParserPtr, carrot::io::MessageHandlerPtr> {
        return std::make_pair<carrot::io::ProtocolParserPtr, carrot::io::MessageHandlerPtr>(
            std::make_unique<carrot::io::LlhttpParser>(std::move(on_message)),
            std::make_unique<carrot::io::HttpEchoHandler>());
      }};

  dispatcher->Run();
  logger.Stop();

  return 0;
}
