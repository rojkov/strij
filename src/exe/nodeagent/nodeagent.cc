#include "core/common/signal_monitor.hh"
#include "core/event/dispatcher_impl.hh"
#include "core/io/tcp_listener.hh"
#include "core/logging/log.hh"

auto main() -> int {
  carrot::event::DispatcherSharedPtr dispatcher = std::make_shared<carrot::event::DispatcherImpl>();
  // Signal monitor must be activated before the logger thread, otherwise we might miss the signal.
  carrot::common::SignalMonitor signal_monitor(dispatcher);

  auto& logger = carrot::logging::Logger::GetInstance();
  logger.Run();

  LOG_REGISTER_THREAD();

  // TODO: have an application-wide connection owner:
  // 1. keeps track of existing connections and deletes closed ones.
  // 2. It should be event::Object to be able to ProcessCommand() (upon closing a connection).

  carrot::io::TcpListener listener{dispatcher, 9090};

  dispatcher->Run();
  logger.Stop();

  return 0;
}
