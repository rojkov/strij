#include "core/common/signal_monitor.hh"
#include "core/event/dispatcher_impl.hh"
#include "core/io/nodeagent_tlv_handler.hh"
#include "core/io/tcp_listener.hh"
#include "core/io/tlv_parser.hh"
#include "core/logging/log.hh"

auto main() -> int {
  carrot::event::DispatcherSharedPtr dispatcher = std::make_shared<carrot::event::DispatcherImpl>();
  // Signal monitor must be activated before the logger thread, otherwise we might miss the signal.
  carrot::common::SignalMonitor signal_monitor(dispatcher);

  auto& logger = carrot::logging::Logger::GetInstance();
  logger.Run();

  LOG_REGISTER_THREAD();

  carrot::io::TcpListener listener{
      dispatcher, 9090,
      [](carrot::io::Connection& conn) -> std::unique_ptr<carrot::io::ProtocolParser> {
        auto handler = std::make_unique<carrot::io::NodeagentTlvHandler>();
        return std::make_unique<carrot::io::TlvParser>(
            [h = std::move(handler), &conn](carrot::io::TlvFrame frame) {
              h->HandleFrame(frame, conn);
            });
      }};

  dispatcher->Run();
  logger.Stop();

  return 0;
}
