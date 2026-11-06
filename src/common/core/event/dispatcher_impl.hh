#pragma once

#include <sys/types.h>

#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "liburing.h"
#include "strij/event/command_handler.hh"
#include "strij/event/completable.hh"
#include "strij/event/dispatcher.hh"

namespace strij::event {

class DispatcherImpl : public Dispatcher, public Completable, public CommandHandler {
public:
  DispatcherImpl();

  // Dispatcher interface
  void Run() override;
  void Shutdown() override;
  void SubmitCommand(Command cmd) override;
  void PrepareAcceptMultishot(Completable* receiver, uint8_t tag, int fdesc) override;
  void PrepareRead(Completable* receiver, uint8_t tag, int fdesc, std::span<std::byte> buf,
                   off_t offset) override;
  void PrepareWrite(Completable* receiver, uint8_t tag, int fdesc, std::span<const std::byte> buf,
                    off_t offset) override;
  void PrepareConnect(Completable* receiver, uint8_t tag, int fdesc, const struct sockaddr* addr,
                      socklen_t addrlen) override;
  void PreparePoll(Completable* receiver, uint8_t tag, int fdesc, uint32_t poll_mask) override;

  // Completable interface
  void HandleCompletion(uint8_t tag, int res, uint32_t flags) override;
  // CommandHandler interface
  void ProcessCommand(Command cmd) override;

private:
  const uint32_t entries_num_{4096};
  struct io_uring ring_{};
  std::vector<Command> command_queue_;
  bool is_finishing_{false};
  absl::Mutex event_fd_mtx_;
  int event_fd_ ABSL_GUARDED_BY(event_fd_mtx_){-1}; // for waking up the event loop
  uint64_t event_fd_val_{0};
};

} // namespace strij::event
