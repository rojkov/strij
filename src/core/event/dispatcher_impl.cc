#include "src/core/event/dispatcher_impl.hh"

#include "carrot/event/io_object.hh"

namespace carrot {
namespace event {

DispatcherImpl::DispatcherImpl() { io_uring_queue_init(entries_num_, &ring_, 0); }

void DispatcherImpl::SubmitCommand(Command cmd) { command_queue_.push_back(cmd); }

void DispatcherImpl::Run() {
  // copied from context_t::run_loop()
  while (true) {
    struct io_uring_cqe* cqe{nullptr};

    while (!command_queue_.empty()) {
      Command cmd = command_queue_.back();
      command_queue_.pop_back();
      cmd.destination_->ProcessCommand(cmd);
    }

    io_uring_submit_and_wait(&ring_, 1);

    // Process all completions in this tick.
    unsigned head{0};
    uint32_t count{0};
    io_uring_for_each_cqe(&ring_, head, cqe) { count++; }
    io_uring_cq_advance(&ring_, head);
  }
}

} // namespace event
} // namespace carrot
