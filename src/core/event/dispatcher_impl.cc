#include "core/event/dispatcher_impl.hh"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cassert>

#include "carrot/event/io_object.hh"

namespace carrot::event {

DispatcherImpl::DispatcherImpl() {
  io_uring_queue_init(entries_num_, &ring_, 0);
  event_fd_ = eventfd(0, 0); // Create an eventfd for waking up the event loop
  if (event_fd_ == -1) {
    perror("eventfd");
    exit(EXIT_FAILURE);
  }
  auto* sqe = io_uring_get_sqe(&ring_);
  assert(sqe != nullptr);
  io_uring_sqe_set_data(sqe, static_cast<IOObject*>(this)); // pre-cast to avoid type erase issues
  io_uring_prep_read(sqe, event_fd_, &event_fd_val_, sizeof(event_fd_val_), 0);
}

auto DispatcherImpl::GetRing() -> struct io_uring* {
  printf("DispatcherImpl::GetRing called... %p\n", this);
  return &ring_;
}

void DispatcherImpl::Run() {
  while (!is_finishing_) {
    struct io_uring_cqe* cqe{nullptr};

    while (!command_queue_.empty()) {
      Command cmd = command_queue_.back();
      command_queue_.pop_back();
      cmd.destination_->ProcessCommand(cmd);
    }

    printf("Submitting and waiting for completions...\n");
    io_uring_submit_and_wait(&ring_, 1);
    printf("Woke up, checking for completions...\n");

    // Process all completions in this tick.
    unsigned head{0};
    unsigned count{0};
    io_uring_for_each_cqe(&ring_, head, cqe) {
      count++;
      printf("t:%d CQE received: res=%d, flags=%u count=%d\n", gettid(), cqe->res, cqe->flags,
             count);
      auto* obj = static_cast<IOObject*>(io_uring_cqe_get_data(cqe));
      if (obj != nullptr) {
        obj->HandleCompletion(cqe->res, cqe->flags);
      }
    }

    io_uring_cq_advance(&ring_, count);
  }
  printf("Exiting run loop, cleaning up io_uring... %p\n", this);

  io_uring_queue_exit(&ring_);
}

void DispatcherImpl::Shutdown() {
  uint64_t val = 1;
  write(event_fd_, &val, sizeof(val));
}

void DispatcherImpl::SubmitCommand(Command cmd) { command_queue_.push_back(cmd); }

void DispatcherImpl::HandleCompletion(int res, uint32_t flags) {
  // This is a simple example of handling a completion for the dispatcher itself.
  // In a real implementation, you would likely have more complex logic here.
  printf("Dispatcher received completion: res=%d, flags=%u\n", res, flags);
  is_finishing_ = true;
  close(event_fd_);
}

void DispatcherImpl::ProcessCommand(Command cmd) {
  // This is a simple example of processing a command for the dispatcher itself.
  // In a real implementation, you would likely have more complex logic here.
  printf("Dispatcher received command: type=%d\n", cmd.type_);
}

void DispatcherImpl::PrepareRead(IOObject* io_object, int fd, std::span<std::byte> buf,
                                 off_t offset) {
  auto* sqe = io_uring_get_sqe(&ring_);
  io_uring_sqe_set_data(sqe, io_object);
  io_uring_prep_read(sqe, fd, buf.data(), buf.size(), offset);
}

} // namespace carrot::event
