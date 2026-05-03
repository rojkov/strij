#pragma once

#include <array>

#include "carrot/event/dispatcher.hh"
#include "carrot/event/io_object.hh"

namespace carrot::io {

namespace {
constexpr size_t BUFFER_SIZE{4096};
}

class Connection : public event::IOObject {
public:
  explicit Connection(int connection_fd, event::DispatcherSharedPtr dispatcher);

  // IOObject interface
  void HandleCompletion(int res, uint32_t flags) override;
  void ProcessCommand(event::Command cmd) override {}

private:
  int fd_;
  event::DispatcherSharedPtr dispatcher_;
  std::array<std::byte, BUFFER_SIZE> buffer_{};
};

} // namespace carrot::io
