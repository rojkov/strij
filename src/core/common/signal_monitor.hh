#pragma once

#include <sys/signalfd.h>

#include "carrot/event/dispatcher.hh"
#include "carrot/event/io_object.hh"

namespace carrot::common {

class SignalMonitor : public event::IOObject {
public:
  explicit SignalMonitor(event::DispatcherSharedPtr dispatcher);

  // event::IOObject overrides
  void HandleCompletion(int res, [[maybe_unused]] uint32_t flags) override;
  void ProcessCommand(event::Command cmd) override;

private:
  struct signalfd_siginfo fdsi_{};
  event::DispatcherSharedPtr dispatcher_;
};

} // namespace carrot::common