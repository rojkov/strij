#include "core/common/signal_monitor.hh"

#include <err.h>

#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstdio>

namespace carrot::common {

SignalMonitor::SignalMonitor(event::DispatcherSharedPtr dispatcher)
    : dispatcher_(std::move(dispatcher)) {

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);

  if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) {
    err(EXIT_FAILURE, "sigprocmask");
  }

  sfd_ = signalfd(-1, &mask, 0);
  if (sfd_ == -1) {
    err(EXIT_FAILURE, "signalfd");
  }

  dispatcher_->PrepareRead(
      this, sfd_, std::as_writable_bytes(std::span<struct signalfd_siginfo, 1>{&fdsi_, 1}), 0);
}

void SignalMonitor::HandleCompletion(int res, [[maybe_unused]] uint32_t flags) {
  assert(dispatcher_ != nullptr);
  assert(res >= 0);

  close(sfd_);
  dispatcher_->Shutdown();
}

void SignalMonitor::ProcessCommand(event::Command cmd) {
  assert(cmd.type_ == event::Command::ACTIVATE_READ);
}

} // namespace carrot::common
