#include "core/io/periodic_timer.hh"

#include <sys/timerfd.h>
#include <unistd.h>

#include <cstdint>
#include <span>

#include "absl/time/time.h"

namespace strij::io {

PeriodicTimer::PeriodicTimer(event::DispatcherSharedPtr dispatcher,
                             std::move_only_function<void()> on_tick)
    : dispatcher_{std::move(dispatcher)}, on_tick_{std::move(on_tick)} {
  timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
}

PeriodicTimer::~PeriodicTimer() {
  if (timer_fd_ >= 0) {
    close(timer_fd_);
  }
}

void PeriodicTimer::Start(absl::Duration interval) {
  struct itimerspec spec {};
  const int64_t nanos = absl::ToInt64Nanoseconds(interval);
  spec.it_value.tv_sec = nanos / 1000000000;
  spec.it_value.tv_nsec = nanos % 1000000000;
  spec.it_interval = spec.it_value;
  timerfd_settime(timer_fd_, 0, &spec, nullptr);
  dispatcher_->PrepareRead(this, kTick, timer_fd_,
                           std::as_writable_bytes(std::span<uint64_t, 1>{&counter_, 1}), 0);
}

void PeriodicTimer::HandleCompletion(uint8_t tag, int res, uint32_t flags) {
  if (tag != kTick) {
    return;
  }
  // The completed io_uring read drained the timerfd counter into counter_.
  on_tick_();
  dispatcher_->PrepareRead(this, kTick, timer_fd_,
                           std::as_writable_bytes(std::span<uint64_t, 1>{&counter_, 1}), 0);
}

} // namespace strij::io
