#pragma once

#include <sys/types.h>

#include <vector>

#include "carrot/event/command_handler.hh"
#include "carrot/event/completable.hh"
#include "carrot/event/dispatcher.hh"
#include "liburing.h"

namespace carrot::event {

class DispatcherImpl : public Dispatcher, public Completable, public CommandHandler {
public:
  DispatcherImpl();

  // Dispatcher interface
  void Run() override;
  void Shutdown() override;
  void SubmitCommand(Command cmd) override;
  void PrepareAcceptMultishot(Completable* io, uint8_t tag, int fd) override;
  void PrepareRead(Completable* io, uint8_t tag, int fd, std::span<std::byte> buf,
                   off_t offset) override;
  void PrepareWrite(Completable* io, uint8_t tag, int fd, std::span<const std::byte> buf,
                    off_t offset) override;
  void PrepareConnect(Completable* io, uint8_t tag, int fd, const struct sockaddr* addr,
                      socklen_t addrlen) override;

  // Completable interface
  void HandleCompletion(uint8_t tag, int res, uint32_t flags) override;
  // CommandHandler interface
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
