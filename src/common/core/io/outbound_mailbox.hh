#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace strij::io {

class Connection;

/**
 * @brief Connection-owned async outbound queue with lifetime tracking.
 *
 * Handlers may retain a shared handle to the mailbox past the call that handed
 * it out (e.g. an async TaskHandler retaining its ResultSender). Enqueue()
 * forwards bytes through the write function the Connection configured and is
 * a no-op once Close() has been called, so a stale handle can never touch a
 * destroyed Connection.
 *
 * Close() is invoked by the owning Connection during teardown; it drops the
 * queue implicitly (the write queue lives in Connection) and fires registered
 * close callbacks. Callbacks run on the event-loop thread and MUST be
 * cleanup-only (no I/O back into the connection).
 */
class OutboundMailbox {
public:
  using CloseCallback = std::move_only_function<void()>;
  using WriteFn = std::move_only_function<void(std::vector<std::byte>)>;

  explicit OutboundMailbox(WriteFn write_fn);
  ~OutboundMailbox() = default;

  OutboundMailbox(const OutboundMailbox&) = delete;
  auto operator=(const OutboundMailbox&) -> OutboundMailbox& = delete;
  OutboundMailbox(OutboundMailbox&&) noexcept = delete;
  auto operator=(OutboundMailbox&&) noexcept -> OutboundMailbox& = delete;

  void Enqueue(std::vector<std::byte> frame);
  auto RegisterOnClose(CloseCallback close_cb) -> std::size_t;
  void UnregisterOnClose(std::size_t token);

  // Optional backpressure: a probe reports the peer connection's current
  // write-queue depth. A non-zero max drops frames once the depth is reached.
  void SetQueueDepthProbe(std::function<std::size_t()> probe);
  void SetMaxQueueDepth(std::size_t max_depth);
  [[nodiscard]] auto DroppedFrames() const -> std::size_t { return dropped_frames_; }

private:
  friend class Connection;
  void Close();

  WriteFn write_fn_;
  bool active_{true};
  std::vector<std::pair<std::size_t, CloseCallback>> close_callbacks_;
  std::size_t next_token_{0};
  std::function<std::size_t()> queue_depth_probe_;
  std::size_t max_queue_depth_{0};
  std::size_t dropped_frames_{0};
};

} // namespace strij::io
