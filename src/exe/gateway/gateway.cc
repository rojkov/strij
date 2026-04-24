#include "core/common/signal_monitor.hh"
#include "core/event/dispatcher_impl.hh"

auto main() -> int {
  carrot::event::DispatcherSharedPtr dispatcher = std::make_shared<carrot::event::DispatcherImpl>();
  carrot::common::SignalMonitor signal_monitor(dispatcher);

  dispatcher->Run();

  return 0;
}
