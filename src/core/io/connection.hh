#pragma once

#include <array>
#include <functional>

#include "carrot/event/dispatcher.hh"
#include "carrot/event/io_object.hh"
#include "core/io/llhttp_parser.hh"

namespace carrot::io {

namespace {
constexpr size_t BUFFER_SIZE{4096};
}

class Reader : public event::IOObject {
public:
  explicit Reader(std::function<void(std::span<std::byte>)>&& on_read_completion);

  // IOObject interface
  void HandleCompletion(int res, uint32_t flags) override;
  void ProcessCommand(event::Command cmd) override {}

  auto Buffer() -> std::span<std::byte> { return buffer_; }

private:
  std::array<std::byte, BUFFER_SIZE> buffer_{};
  std::function<void(std::span<std::byte>)> on_read_completion_;
};

class Connection final {
public:
  explicit Connection(int connection_fd, event::DispatcherSharedPtr dispatcher);

private:
  void onReadCompletion(std::span<std::byte> data);

  int fd_;
  event::DispatcherSharedPtr dispatcher_;
  Reader reader_;
};

} // namespace carrot::io
