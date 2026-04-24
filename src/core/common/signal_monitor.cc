#include "core/common/signal_monitor.hh"

#include <err.h>

#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstdio>

namespace carrot::common {

SignalMonitor::SignalMonitor(event::DispatcherSharedPtr dispatcher)
    : dispatcher_(std::move(dispatcher)) {

  struct io_uring* ring = dispatcher_->GetRing();
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGQUIT);

  if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) {
    err(EXIT_FAILURE, "sigprocmask");
  }

  int sfd = signalfd(-1, &mask, 0);
  if (sfd == -1) {
    err(EXIT_FAILURE, "signalfd");
  }

  auto* sqe = io_uring_get_sqe(ring);
  io_uring_sqe_set_data(sqe, this);

  io_uring_prep_read(sqe, sfd, &fdsi_, sizeof(fdsi_), 0);
}

void SignalMonitor::HandleCompletion(int res, [[maybe_unused]] uint32_t flags) {
  assert(dispatcher_ != nullptr);
  assert(res >= 0);

  printf("signal handled\n");
  dispatcher_->Shutdown();
}

void SignalMonitor::ProcessCommand(event::Command cmd) {
  assert(cmd.type_ == event::Command::ACTIVATE_READ);
}

} // namespace carrot::common