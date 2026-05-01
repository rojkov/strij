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

  LOG_INFO("Hello from the main thread! {} {}", 42, "literal string");

  {
    std::string_view test_str = "test string";
    std::string_view test_str2 = "jjjjjjj";

    LOG_INFO("Second line {} {}", test_str, test_str2);
  }

  {
    // Test how the logger handles a scoped string; the string data should be copied correctly and
    // not cause use-after-free.
    std::string temp_str = "temporary string";
    LOG_INFO("Logging a scoped string: {}", temp_str);
  }

  LOG_DEBUG("{}", 1);
  LOG_INFO("{}", 1);
  LOG_WARNING("{}", 1);
  LOG_ERROR("{}", 1);

  dispatcher->Run();
  logger.Stop();

  return 0;
}
