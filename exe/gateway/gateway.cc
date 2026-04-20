#include <err.h>
#include <sys/signalfd.h>

#include <cassert>

#include "src/core/event/dispatcher_impl.hh"

#include "carrot/event/io_object.hh"

// TODO:
// make an Application class derived from io_object_t to handle completions in
// the io_uring.
namespace {

class Application : public carrot::event::IOObject {
public:
  void HandleCompletion(int res, uint32_t flags) override {}
  void ProcessCommand(carrot::event::Command cmd) override {
    // assert(cmd.type_ == carrot::event::Command::ACTIVATE_READ);
    auto* dispatcher = static_cast<carrot::event::Dispatcher*>(cmd.args_);
    struct io_uring* ring = dispatcher->GetRing();
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);

    if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) {
      err(EXIT_FAILURE, "sigprocmask");
    }

    int sfd = signalfd(-1, &mask, 0);

    auto sqe = io_uring_get_sqe(ring);

    io_uring_prep_read(sqe, sfd, &fdsi_, sizeof(fdsi_), 0);
  }

private:
  struct signalfd_siginfo fdsi_;
};

} // namespace

int main() {
  carrot::event::DispatcherPtr dispatcher = std::make_unique<carrot::event::DispatcherImpl>();
  Application app;
  carrot::event::Command activate_signal_handling{.destination_ = &app, .args_ = dispatcher.get()};
  dispatcher->SubmitCommand(activate_signal_handling);

  dispatcher->Run();

  return 0;
}
