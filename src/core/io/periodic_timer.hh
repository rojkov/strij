#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "absl/time/time.h"
#include "strij/event/completable.hh"
#include "strij/event/dispatcher.hh"

namespace strij::io {

// Recurring timer driven by the event loop. Arms a timerfd and issues an
// io_uring read for the expiry counter; each completion fires `on_tick` and
// re-arms the read, so the callback runs on the event-loop thread at most once
// per interval (multiple overdue expirations coalesce into the counter).
class PeriodicTimer final : public event::Completable {
public:
  PeriodicTimer(event::DispatcherSharedPtr dispatcher, std::move_only_function<void()> on_tick);
  ~PeriodicTimer() override;

  PeriodicTimer(const PeriodicTimer&) = delete;
  auto operator=(const PeriodicTimer&) -> PeriodicTimer& = delete;
  PeriodicTimer(PeriodicTimer&&) noexcept = delete;
  auto operator=(PeriodicTimer&&) noexcept -> PeriodicTimer& = delete;

  // Arms the timer to fire every `interval` seconds. `interval` must be > 0.
  void Start(absl::Duration interval);

  // Completable interface
  void HandleCompletion(uint8_t tag, int res, uint32_t flags) override;

private:
  enum Tags : uint8_t { kTick = 0 };

  event::DispatcherSharedPtr dispatcher_;
  std::move_only_function<void()> on_tick_;
  int timer_fd_{-1};
  uint64_t counter_{0};
};

} // namespace strij::io
