#include "core/logging/log_frontend.hh"

#include <sys/eventfd.h>

#include <iostream>

#include "src/core/logging/log_frontend.hh"

namespace carrot::logging {

LogFrontend::LogFrontend(event::DispatcherSharedPtr dispatcher)
    : event_fd_{eventfd(0, EFD_NONBLOCK)}, event_fd_val_{0} {
  if (event_fd_ == -1) {
    perror("eventfd");
    exit(EXIT_FAILURE);
  }

  auto* sqe = io_uring_get_sqe(dispatcher->GetRing());
  io_uring_sqe_set_data(sqe, this);
  io_uring_prep_read(sqe, event_fd_, &event_fd_val_, sizeof(event_fd_val_), 0);
  dispatcher_ = std::move(dispatcher);
};

void LogFrontend::Log(const std::string& msg) {
  queue_.emplace(msg);
  uint64_t val = 1; // Value doesn't matter, we just need to wake up the event loop
  write(event_fd_, &val, sizeof(val));
}

void LogFrontend::HandleCompletion(int res, uint32_t flags) {
  printf("LogFrontend::HandleCompletion called with res=%d, flags=%u\n", res, flags);
  if (res < 0) {
    perror("eventfd read");
    return;
  }
  // Process all log entries in the queue
  while (queue_.front()) {
    std::cout << *queue_.front() << std::endl;
    queue_.pop();
  }

  // Re-arm the eventfd for the next log entry
  auto* sqe = io_uring_get_sqe(dispatcher_->GetRing());
  io_uring_sqe_set_data(sqe, this);
  io_uring_prep_read(sqe, event_fd_, &event_fd_val_, sizeof(event_fd_val_), 0);
}

void LogFrontend::ProcessCommand(event::Command cmd) {
  // No commands expected for now
  std::cerr << "Unexpected command received in LogFrontend" << std::endl;
}

} // namespace carrot::logging