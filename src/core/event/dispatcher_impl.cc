#include "core/event/dispatcher_impl.hh"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cassert>
#include <cstdint>

#include "strij/event/completable.hh"
#include "include/strij/event/completable.hh"

namespace strij::event {

const uintptr_t tag_mask{7};

namespace {

inline auto merge_with_tag(Completable* completable, uint8_t tag) -> void* {
  assert((tag & ~tag_mask) == 0);

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(completable) | tag);
}

} // namespace

DispatcherImpl::DispatcherImpl() : event_fd_{eventfd(0, 0)} {
  io_uring_queue_init(entries_num_, &ring_, 0);
  if (event_fd_ == -1) {
    perror("eventfd");
    throw std::runtime_error("unable to open event fd");
  }

  PrepareRead(this, 0, event_fd_, std::as_writable_bytes(std::span<uint64_t, 1>{&event_fd_val_, 1}),
              0);
}

void DispatcherImpl::Run() {
  while (!is_finishing_) {
    struct io_uring_cqe* cqe{nullptr};

    while (!command_queue_.empty()) {
      Command cmd = command_queue_.back();
      command_queue_.pop_back();
      cmd.destination_->ProcessCommand(cmd);
    }

    io_uring_submit_and_wait(&ring_, 1);

    // Process all completions in this tick.
    unsigned head{0};
    unsigned count{0};
    io_uring_for_each_cqe(&ring_, head, cqe) {
      count++;
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      auto user_data = reinterpret_cast<uintptr_t>(io_uring_cqe_get_data(cqe));
      uint8_t tag = user_data & tag_mask;
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      auto* completable = reinterpret_cast<Completable*>(user_data & ~tag_mask);
      if (completable != nullptr) {
        completable->HandleCompletion(tag, cqe->res, cqe->flags);
      }
    }

    io_uring_cq_advance(&ring_, count);
  }

  io_uring_queue_exit(&ring_);
}

void DispatcherImpl::Shutdown() {
  uint64_t val = 1;
  write(event_fd_, &val, sizeof(val));
}

void DispatcherImpl::SubmitCommand(Command cmd) { command_queue_.push_back(cmd); }

void DispatcherImpl::HandleCompletion([[maybe_unused]] uint8_t tag, [[maybe_unused]] int res,
                                      [[maybe_unused]] uint32_t flags) {
  is_finishing_ = true;
  // We don't re-arm this event_fd_ after this point -> closing.
  close(event_fd_);
}

void DispatcherImpl::ProcessCommand(Command cmd) {
  // This is a simple example of processing a command for the dispatcher itself.
  // In a real implementation, you would likely have more complex logic here.
}

void DispatcherImpl::PrepareAcceptMultishot(Completable* receiver, uint8_t tag, int fdesc) {
  auto* sqe = io_uring_get_sqe(&ring_);
  assert(sqe != nullptr);
  io_uring_sqe_set_data(sqe, merge_with_tag(receiver, tag));
  io_uring_prep_multishot_accept(sqe, fdesc, nullptr, nullptr, 0);
}

void DispatcherImpl::PrepareRead(Completable* receiver, uint8_t tag, int fdesc,
                                 std::span<std::byte> buf, off_t offset) {
  auto* sqe = io_uring_get_sqe(&ring_);
  assert(sqe != nullptr);
  io_uring_sqe_set_data(sqe, merge_with_tag(receiver, tag));
  io_uring_prep_read(sqe, fdesc, buf.data(), buf.size(), offset);
}

void DispatcherImpl::PrepareWrite(Completable* receiver, uint8_t tag, int fdesc,
                                  std::span<const std::byte> buf, off_t offset) {
  auto* sqe = io_uring_get_sqe(&ring_);
  assert(sqe != nullptr);
  io_uring_sqe_set_data(sqe, merge_with_tag(receiver, tag));
  io_uring_prep_write(sqe, fdesc, buf.data(), buf.size(), offset);
}

void DispatcherImpl::PrepareConnect(Completable* receiver, uint8_t tag, int fdesc,
                                    const struct sockaddr* addr, socklen_t addrlen) {
  auto* sqe = io_uring_get_sqe(&ring_);
  assert(sqe != nullptr);
  io_uring_sqe_set_data(sqe, merge_with_tag(receiver, tag));
  io_uring_prep_connect(sqe, fdesc, addr, addrlen);
}

void DispatcherImpl::PreparePoll(Completable* receiver, uint8_t tag, int fdesc,
                                 uint32_t poll_mask) {
  auto* sqe = io_uring_get_sqe(&ring_);
  assert(sqe != nullptr);
  io_uring_sqe_set_data(sqe, merge_with_tag(receiver, tag));
  io_uring_prep_poll_add(sqe, fdesc, poll_mask);
}

} // namespace strij::event
