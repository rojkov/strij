#include "core/common/signal_monitor.hh"
#include "core/event/dispatcher_impl.hh"
#include "core/logging/log.hh"

auto main() -> int {
  carrot::event::DispatcherSharedPtr dispatcher = std::make_shared<carrot::event::DispatcherImpl>();
  // Signal monitor must be activated before the logger thread, otherwise we might miss the signal.
  carrot::common::SignalMonitor signal_monitor(dispatcher);

  auto& logger = carrot::logging::Logger::GetInstance();
  logger.Run();

  LOG_REGISTER_THREAD();

  LOG("Hello from the main thread!");
  dispatcher->Run();
  logger.Stop();

  return 0;
}
