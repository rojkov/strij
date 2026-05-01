#pragma once

#include <chrono>
#include <cstring>
#include <format>
#include <iostream>
#include <source_location>
#include <string>
#include <tuple>
#include <type_traits>

#include "carrot/event/dispatcher.hh"
#include "carrot/event/io_object.hh"
#include "rigtorp/SPSCQueue.h"

namespace carrot::logging {

struct LogEntry {
  void (*format_fn_)(const char* fmt_str, const std::byte* data, std::string& out);

  const char* fmt_str_;
  std::chrono::system_clock::time_point timestamp_;
  uint32_t thread_id_;
  std::source_location location_;
  alignas(16) std::byte args_data_[512];
};

template <typename T> struct Unpacker {
  static T unpack(const std::byte*& ptr) {
    T val;
    std::memcpy(&val, ptr, sizeof(T));
    ptr += sizeof(T);
    return val;
  }
};

template <> struct Unpacker<char const*> {
  static char const* unpack(const std::byte*& ptr) {
    char const* val{reinterpret_cast<char const*>(ptr)};
    ptr += sizeof(char const*);
    return val;
  }
};

// Specialization for strings: read length, then content
template <> struct Unpacker<std::string_view> {
  static std::string_view unpack(const std::byte*& ptr) {
    size_t len = Unpacker<size_t>::unpack(ptr);
    std::string_view sv(reinterpret_cast<const char*>(ptr), len);
    ptr += len;
    return sv;
  }
};

template <> struct Unpacker<std::string> {
  static std::string unpack(const std::byte*& ptr) {
    size_t len = Unpacker<size_t>::unpack(ptr);
    std::string s(reinterpret_cast<const char*>(ptr), len);
    ptr += len;
    return s;
  }
};

template <typename... Args>
void format_trampoline(const char* fmt_str, const std::byte* data, std::string& out) {
  const std::byte* ptr = data;
  auto args_tuple = std::tuple<std::decay_t<Args>...>{Unpacker<std::decay_t<Args>>::unpack(ptr)...};
  // Unpack every argument into a std::format-compatible dynamic format list
  out = std::apply(
      [fmt_str](auto&... args) { return std::vformat(fmt_str, std::make_format_args(args...)); },
      args_tuple);
}

// Helper to deduce argument types and return the right function pointer
template <typename... Args>
inline auto get_format_fn() -> void (*)(const char*, const std::byte*, std::string&) {
  return &format_trampoline<std::decay_t<Args>...>;
}

template <typename T> void pack_arg(std::byte*& ptr, const T& value) {
  static_assert(std::is_trivially_copyable_v<T>,
                "pack_arg requires trivially copyable types or a custom overload");
  std::memcpy(ptr, &value, sizeof(T));
  ptr += sizeof(T);
}

// Specializations for strings
void pack_arg(std::byte*& ptr, std::string_view sw);
void pack_arg(std::byte*& ptr, const std::string& value);

class LogFrontend : public event::IOObject {
public:
  explicit LogFrontend(event::DispatcherSharedPtr dispatcher);
  ~LogFrontend();

  void Log(LogEntry&& entry);

  void LogForDebug(LogEntry&& entry) {
    std::string output;
    entry.format_fn_(entry.fmt_str_, entry.args_data_, output);
    std::cout << "LogForDebug: " << output << std::endl;
  }

  // event::IOObject interface
  void HandleCompletion(int res, uint32_t flags) override;
  void ProcessCommand(event::Command cmd) override;

private:
  static constexpr std::size_t queue_size = 1024;
  rigtorp::SPSCQueue<LogEntry> queue_{queue_size};
  int event_fd_;
  uint64_t event_fd_val_;
  event::DispatcherSharedPtr dispatcher_;
};

} // namespace carrot::logging
