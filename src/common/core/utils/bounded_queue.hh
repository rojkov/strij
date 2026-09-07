#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <utility>

namespace strij::utils {

// Single-threaded FIFO queue with a caller-specified maximum size. Owned and
// used by one event-loop context (a local scheduler); it does not synchronize
// internally. A full queue rejects Push(), which is the "reject the incoming
// probe immediately" semantics deferred admission needs.
template <typename T> class BoundedQueue {
public:
  explicit BoundedQueue(size_t max_size) : max_size_{max_size} {}

  // Appends `item` to the back, returning true; returns false unchanged when
  // the queue is Full().
  auto Push(T item) -> bool {
    if (Full()) {
      return false;
    }

    queue_.push_back(std::move(item));
    return true;
  }

  // Removes and returns the front element, or disengages when empty. FIFO.
  auto TryPop() -> std::optional<T> {
    if (queue_.empty()) {
      return std::nullopt;
    }

    T item = std::move(queue_.front());
    queue_.pop_front();
    return item;
  }

  // Pointer to the front element without removal; null when empty. Valid until
  // the next mutation.
  [[nodiscard]] auto Peek() const -> const T* {
    if (queue_.empty()) {
      return nullptr;
    }

    return &queue_.front();
  }

  [[nodiscard]] auto Size() const -> size_t { return queue_.size(); }
  [[nodiscard]] auto Empty() const -> bool { return queue_.empty(); }
  [[nodiscard]] auto Full() const -> bool { return queue_.size() >= max_size_; }
  [[nodiscard]] auto MaxSize() const -> size_t { return max_size_; }

  void Clear() { queue_.clear(); }

private:
  size_t max_size_;
  std::deque<T> queue_;
};

} // namespace strij::utils