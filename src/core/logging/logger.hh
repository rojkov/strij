#pragma once

#include <mutex>
#include <thread>
#include <vector>

#include "carrot/event/dispatcher.hh"
#include "core/logging/log_frontend.hh"

namespace carrot::logging {

class Logger {
public:
  static auto GetInstance() -> Logger&;

  static thread_local LogFrontend* local_context_;

  void RegisterThread();
  void Run();
  void Stop();

private:
  Logger();

  std::mutex mtx_;
  std::vector<LogFrontend*> worker_queues_;
  event::DispatcherSharedPtr dispatcher_;
  std::thread thread_;
};

inline thread_local LogFrontend* Logger::local_context_ = nullptr;

} // namespace carrot::logging