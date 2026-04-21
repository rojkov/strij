#include "core/event/dispatcher_impl.hh"

#include "carrot/event/io_object.hh"

namespace carrot::event {

DispatcherImpl::DispatcherImpl() { io_uring_queue_init(entries_num_, &ring_, 0); }

auto DispatcherImpl::GetRing() -> struct io_uring* { return &ring_; }

void DispatcherImpl::Run() {
  // copied from context_t::run_loop()
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
    uint32_t count{0};
    io_uring_for_each_cqe(&ring_, head, cqe) {
      count++;
      auto* obj = static_cast<IOObject*>(io_uring_cqe_get_data(cqe));
      if (obj != nullptr) {
        obj->HandleCompletion(cqe->res, cqe->flags);
      }
    }

    io_uring_cq_advance(&ring_, head);
  }

  io_uring_queue_exit(&ring_);
}

void DispatcherImpl::Shutdown() { is_finishing_ = true; }

void DispatcherImpl::SubmitCommand(Command cmd) { command_queue_.push_back(cmd); }

} // namespace carrot::event