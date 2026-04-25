#pragma once

#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
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

  absl::Mutex worker_queues_mtx_;
  std::vector<LogFrontend*> worker_queues_ ABSL_GUARDED_BY(worker_queues_mtx_);
  event::DispatcherSharedPtr dispatcher_;
  std::thread thread_;
};

inline thread_local LogFrontend* Logger::local_context_ = nullptr;

} // namespace carrot::logging