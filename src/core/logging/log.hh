#pragma once

#include <unistd.h>

#include "core/logging/logger.hh"

#define LOG_REGISTER_THREAD() carrot::logging::Logger::GetInstance().RegisterThread()

namespace carrot::logging {

template <typename... Args>
inline void write_stderr_fallback(LogEntry::severity severity, std::source_location const& location,
                                  const char* fmt_str, Args&&... args) {
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  auto local_time = *std::localtime(&time);
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()) % 1000000;

  std::string msg = std::vformat(fmt_str, std::make_format_args(args...));

  std::string buf = std::format("{} {:02}:{:02}:{:02}.{:06} {} [early] {}:{} {}\n",
                                static_cast<char>(severity), local_time.tm_hour, local_time.tm_min,
                                local_time.tm_sec, us.count(), gettid(), location.file_name(),
                                location.line(), msg);

  write(STDERR_FILENO, buf.data(), buf.size());
}

// Helper function that deduces argument types and logs them
template <typename... Args>
inline void log_impl(LogEntry::severity severity, std::source_location&& location,
                     const char* fmt_str, Args&&... args) {
  if (carrot::logging::Logger::local_context_ != nullptr) {
    carrot::logging::LogEntry entry;
    entry.severity_ = severity;
    entry.timestamp_ = std::chrono::system_clock::now();
    entry.thread_id_ = gettid();
    entry.location_ = std::move(location);
    entry.fmt_str_ = fmt_str;
    entry.format_fn_ = get_format_fn<Args...>();
    // This ptr may be unused if Args are empty.
    [[maybe_unused]] std::byte* ptr = entry.args_data_;
    (pack_arg(ptr, args), ...);
    carrot::logging::Logger::local_context_->Log(std::move(entry));
  } else {
    carrot::logging::write_stderr_fallback(severity, location, fmt_str, std::forward<Args>(args)...);
  }
}

} // namespace carrot::logging

#define LOG(s, format, ...)                                                                        \
  carrot::logging::log_impl(carrot::logging::LogEntry::s, std::source_location::current(), format, \
                            ##__VA_ARGS__)

#define LOG_DEBUG(format, ...) LOG(DEBUG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) LOG(INFO, format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...) LOG(WARNING, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) LOG(ERROR, format, ##__VA_ARGS__)
