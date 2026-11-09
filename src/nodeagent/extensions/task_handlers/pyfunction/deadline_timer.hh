#pragma once

#include <cstdint>
#include <functional>

#include "absl/time/time.h"
#include "strij/event/completable.hh"
#include "strij/event/dispatcher.hh"

namespace strij::nodeagent::task_handlers::pyfunction {

// One-shot timerfd-backed deadline. The owner arms it with the earliest
// deadline it cares about and re-arms after every expiry or state change. This
// keeps the event loop free of a periodic poll when nothing is pending.
class DeadlineTimer final : public event::Completable {
public:
  DeadlineTimer(event::Dispatcher& dispatcher, std::move_only_function<void()> on_expire);
  ~DeadlineTimer() override;

  DeadlineTimer(const DeadlineTimer&) = delete;
  auto operator=(const DeadlineTimer&) -> DeadlineTimer& = delete;
  DeadlineTimer(DeadlineTimer&&) noexcept = delete;
  auto operator=(DeadlineTimer&&) noexcept -> DeadlineTimer& = delete;

  // Arms the timer for `deadline`. Earlier than the current deadline replaces
  // it; later is ignored until the current one fires.
  void Schedule(absl::Time deadline);
  void Cancel();

  void HandleCompletion(uint8_t tag, int res, uint32_t flags) override;

private:
  enum Tags : uint8_t { kExpire = 0 };

  void arm(absl::Time deadline);

  event::Dispatcher* dispatcher_;
  std::move_only_function<void()> on_expire_;
  int timer_fd_{-1};
  bool poll_pending_{false};
  bool armed_{false};
  absl::Time deadline_{absl::InfinitePast()};
};

} // namespace strij::nodeagent::task_handlers::pyfunction
