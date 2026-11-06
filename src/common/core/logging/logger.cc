#include "common/core/logging/logger.hh"

#include <sys/eventfd.h>

#include "common/core/event/dispatcher_impl.hh"

namespace strij::logging {

Logger::Logger() : dispatcher_{std::make_shared<event::DispatcherImpl>()} {}

auto Logger::GetInstance() -> Logger& {
  static Logger instance;
  return instance;
}

void Logger::RegisterThread() {
  auto ctx = std::make_unique<LogFrontend>(dispatcher_);
  local_context_ = ctx.get();

  {
    absl::MutexLock lock(worker_queues_mtx_);
    worker_queues_.push_back(std::move(ctx));
  }
}

void Logger::Run() {
  thread_ = std::thread([this]() -> void { dispatcher_->Run(); });
}

void Logger::Stop() {
  dispatcher_->Shutdown();

  if (thread_.joinable()) {
    thread_.join();
  }

  local_context_ = nullptr;

  {
    absl::MutexLock lock(worker_queues_mtx_);
    worker_queues_.clear();
  }
}

} // namespace strij::logging