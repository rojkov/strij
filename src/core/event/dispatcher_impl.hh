#pragma once

#include <sys/types.h>

#include <vector>

#include "carrot/event/dispatcher.hh"
#include "carrot/event/io_object.hh"
#include "liburing.h"

namespace carrot::event {

class DispatcherImpl : public Dispatcher, public IOObject {
public:
  DispatcherImpl();

  // Dispatcher interface
  void Run() override;
  void Shutdown() override;
  void SubmitCommand(Command cmd) override;
  void PrepareAcceptMultishot(IOObject* io_object, int fd) override;
  void PrepareRead(IOObject* io_object, int fd, std::span<std::byte> buf, off_t offset) override;
  void PrepareWrite(IOObject* io_object, int fd, std::span<const std::byte> buf,
                    off_t offset) override;

  // IOObject interface
  void HandleCompletion(int res, uint32_t flags) override;
  void ProcessCommand(Command cmd) override;

private:
  const uint32_t entries_num_{4096};
  struct io_uring ring_{};
  std::vector<Command> command_queue_;
  bool is_finishing_{false};
  int event_fd_{-1};
  uint64_t event_fd_val_{0};
};

} // namespace carrot::event
