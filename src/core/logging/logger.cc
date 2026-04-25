#include "core/logging/logger.hh"

#include <absl/synchronization/mutex.h>
#include <sys/eventfd.h>

#include "core/event/dispatcher_impl.hh"

namespace carrot::logging {

Logger::Logger() : dispatcher_{std::make_shared<event::DispatcherImpl>()} {}

auto Logger::GetInstance() -> Logger& {
  static Logger instance;
  return instance;
}

void Logger::RegisterThread() {
  auto* ctx = new LogFrontend(dispatcher_);
  local_context_ = ctx;

  {
    absl::MutexLock lock(worker_queues_mtx_);
    worker_queues_.push_back(ctx);
  }
}

void Logger::Run() {
  thread_ = std::thread([this]() { dispatcher_->Run(); });
}

void Logger::Stop() {
  printf("Shutting down logger...\n");
  dispatcher_->Shutdown();
  if (thread_.joinable()) {
    thread_.join();
    printf("Logger stopped.\n");
  }
}

} // namespace carrot::logging