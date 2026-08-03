#pragma once

#include <sys/signalfd.h>

#include "strij/event/completable.hh"
#include "strij/event/dispatcher.hh"

namespace strij::common {

class SignalMonitor : public event::Completable {
public:
  explicit SignalMonitor(event::DispatcherSharedPtr dispatcher);

  // event::Completable interface
  void HandleCompletion(uint8_t tag, int res, [[maybe_unused]] uint32_t flags) override;

private:
  struct signalfd_siginfo fdsi_{};
  event::DispatcherSharedPtr dispatcher_;
  int sfd_;
};

} // namespace strij::common
