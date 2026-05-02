#include "core/logging/log_frontend.hh"

#include <sys/eventfd.h>

#include <iostream>

#include "src/core/logging/log_frontend.hh"

namespace carrot::logging {

void pack_arg(std::byte*& ptr, std::string_view sw) {
  size_t len = sw.size();
  pack_arg(ptr, len);
  std::memcpy(ptr, sw.data(), len);
  ptr += len;
}

void pack_arg(std::byte*& ptr, const std::string& value) { pack_arg(ptr, std::string_view(value)); }

LogFrontend::LogFrontend(event::DispatcherSharedPtr dispatcher)
    : event_fd_{eventfd(0, EFD_NONBLOCK)}, event_fd_val_{0}, dispatcher_{std::move(dispatcher)} {
  if (event_fd_ == -1) {
    perror("eventfd");
    exit(EXIT_FAILURE);
  }

  dispatcher_->PrepareRead(this, event_fd_,
                           std::as_writable_bytes(std::span<uint64_t, 1>{&event_fd_val_, 1}), 0);
};

LogFrontend::~LogFrontend() { close(event_fd_); }

void LogFrontend::Log(LogEntry&& entry) {
  queue_.emplace(std::move(entry));
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
    std::string output;
    auto* entry = queue_.front();
    entry->format_fn_(entry->fmt_str_, entry->args_data_, output);
    auto time = std::chrono::system_clock::to_time_t(entry->timestamp_);
    auto local_time = *std::localtime(&time);
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                            entry->timestamp_.time_since_epoch()) %
                        1000000;

    std::cout << std::format(
                     "{} {:02}:{:02}:{:02}.{:06} {} {}:{} ", static_cast<char>(entry->severity_),
                     local_time.tm_hour, local_time.tm_min, local_time.tm_sec, microseconds.count(),
                     entry->thread_id_, entry->location_.file_name(), entry->location_.line())
              << output << std::endl;
    queue_.pop();
  }

  // Re-arm the eventfd for the next log entry
  dispatcher_->PrepareRead(this, event_fd_,
                           std::as_writable_bytes(std::span<uint64_t, 1>{&event_fd_val_, 1}), 0);
}

void LogFrontend::ProcessCommand(event::Command cmd) {
  // No commands expected for now
  std::cerr << "Unexpected command received in LogFrontend" << std::endl;
}

} // namespace carrot::logging
