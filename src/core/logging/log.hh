#pragma once

#include <chrono>

#include "core/logging/logger.hh"

#define LOG_REGISTER_THREAD() carrot::logging::Logger::GetInstance().RegisterThread()

namespace carrot::logging {

// Helper function that deduces argument types and logs them
template <typename... Args> inline void log_impl(const char* fmt_str, Args&&... args) {
  if (carrot::logging::Logger::local_context_ != nullptr) {
    carrot::logging::LogEntry entry;
    entry.timestamp_ = std::chrono::system_clock::now();
    entry.thread_id_ = gettid();
    entry.fmt_str_ = fmt_str;
    entry.format_fn_ = get_format_fn<Args...>();
    std::byte* ptr = entry.args_data_;
    (pack_arg(ptr, args), ...);
    // carrot::logging::Logger::local_context_->LogForDebug(std::move(entry));
    carrot::logging::Logger::local_context_->Log(std::move(entry));
  }
}

} // namespace carrot::logging

#define LOG(format, ...) carrot::logging::log_impl(format, __VA_ARGS__)
