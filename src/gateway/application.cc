#include "gateway/application.hh"

#include <err.h>

#include <cassert>
#include <cstdint>

namespace carrot::gateway {

Application::Application(carrot::event::DispatcherSharedPtr dispatcher)
    : dispatcher_(std::move(dispatcher)) {}

void Application::HandleCompletion(int res, [[maybe_unused]] uint32_t flags) {
  assert(dispatcher_ != nullptr);
  assert(res >= 0);

  dispatcher_->Shutdown();
}

void Application::ProcessCommand(event::Command cmd) {
  assert(cmd.type_ == event::Command::ACTIVATE_READ);

  struct io_uring* ring = dispatcher_->GetRing();
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);

  if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) {
    err(EXIT_FAILURE, "sigprocmask");
  }

  int sfd = signalfd(-1, &mask, 0);

  auto* sqe = io_uring_get_sqe(ring);
  io_uring_sqe_set_data(sqe, this);

  io_uring_prep_read(sqe, sfd, &fdsi_, sizeof(fdsi_), 0);
}

} // namespace carrot::gateway