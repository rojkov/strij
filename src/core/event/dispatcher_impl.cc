#include "src/core/event/dispatcher_impl.hh"

namespace carrot {
namespace event {

DispatcherImpl::DispatcherImpl() { io_uring_queue_init(entries_num_, &ring_, 0); }

void DispatcherImpl::Run() {
  // copied from context_t::run_loop()
  while (true) {
    struct io_uring_cqe* cqe{nullptr};

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
