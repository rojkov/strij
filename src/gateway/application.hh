#pragma once

#include <sys/signalfd.h>

#include "carrot/event/dispatcher.hh"
#include "carrot/event/io_object.hh"

namespace carrot::gateway {

class Application : public event::IOObject {
public:
  explicit Application(event::DispatcherSharedPtr dispatcher);

  // event::IOObject overrides
  void HandleCompletion(int res, [[maybe_unused]] uint32_t flags) override;
  void ProcessCommand(event::Command cmd) override;

private:
  struct signalfd_siginfo fdsi_{};
  event::DispatcherSharedPtr dispatcher_;
};

} // namespace carrot::gateway