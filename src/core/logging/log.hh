#pragma once

#include "core/logging/logger.hh"

#define LOG_REGISTER_THREAD() carrot::logging::Logger::GetInstance().RegisterThread()

namespace carrot::logging {

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
    std::byte* ptr = entry.args_data_;
    (pack_arg(ptr, args), ...);
    carrot::logging::Logger::local_context_->Log(std::move(entry));
  }
}

} // namespace carrot::logging

#define LOG(s, format, ...)                                                                        \
  carrot::logging::log_impl(carrot::logging::LogEntry::s, std::source_location::current(), format, \
                            __VA_ARGS__)

#define LOG_DEBUG(format, ...) LOG(DEBUG, format, __VA_ARGS__)
#define LOG_INFO(format, ...) LOG(INFO, format, __VA_ARGS__)
#define LOG_WARNING(format, ...) LOG(WARNING, format, __VA_ARGS__)
#define LOG_ERROR(format, ...) LOG(ERROR, format, __VA_ARGS__)
