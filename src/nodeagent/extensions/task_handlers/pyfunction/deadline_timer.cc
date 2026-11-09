#include "nodeagent/extensions/task_handlers/pyfunction/deadline_timer.hh"

#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <utility>

#include "common/core/logging/log.hh"

namespace strij::nodeagent::task_handlers::pyfunction {

namespace {

const int64_t kBillion = 1'000'000'000;

} // namespace

DeadlineTimer::DeadlineTimer(event::Dispatcher& dispatcher,
                             std::move_only_function<void()> on_expire)
    : dispatcher_{&dispatcher}, on_expire_{std::move(on_expire)},
      timer_fd_{::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)} {
  if (timer_fd_ < 0) {
    LOG_WARNING("pyfunction: timerfd_create failed (errno={})", errno);
  }
}

DeadlineTimer::~DeadlineTimer() {
  if (timer_fd_ >= 0) {
    ::close(timer_fd_);
  }
}

void DeadlineTimer::Schedule(absl::Time deadline) {
  if (timer_fd_ < 0) {
    return;
  }

  if (armed_ && deadline >= deadline_) {
    return;
  }

  arm(deadline);
}

void DeadlineTimer::Cancel() {
  if (timer_fd_ < 0) {
    return;
  }

  const struct itimerspec disarm{};
  (void)::timerfd_settime(timer_fd_, 0, &disarm, nullptr);
  armed_ = false;
  deadline_ = absl::InfinitePast();
}

void DeadlineTimer::arm(absl::Time deadline) {
  const absl::Duration remaining = deadline - absl::Now();
  const std::int64_t nanos = std::max<std::int64_t>(0, absl::ToInt64Nanoseconds(remaining));

  struct itimerspec spec{};
  spec.it_value.tv_sec = nanos / kBillion;
  spec.it_value.tv_nsec = nanos % kBillion;

  if (::timerfd_settime(timer_fd_, 0, &spec, nullptr) != 0) {
    LOG_WARNING("pyfunction: timerfd_settime failed (errno={})", errno);
    return;
  }

  armed_ = true;
  deadline_ = deadline;

  if (!poll_pending_) {
    dispatcher_->PreparePoll(this, kExpire, timer_fd_, POLLIN);
    poll_pending_ = true;
  }
}

void DeadlineTimer::HandleCompletion(uint8_t /*tag*/, int /*res*/, uint32_t /*flags*/) {
  std::uint64_t expirations = 0;
  (void)::read(timer_fd_, &expirations, sizeof(expirations));
  poll_pending_ = false;
  armed_ = false;
  deadline_ = absl::InfinitePast();

  if (on_expire_) {
    on_expire_();
  }
}

} // namespace strij::nodeagent::task_handlers::pyfunction
