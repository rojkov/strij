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
 * forwards bytes to the connection's write queue and is a no-op once Close()
 * has been called, so a stale handle can never touch a destroyed Connection.
 *
 * Close() is invoked by the owning Connection during teardown; it drops the
 * queue implicitly (the write queue lives in Connection) and fires registered
 * close callbacks. Callbacks run on the event-loop thread and MUST be
 * cleanup-only (no I/O back into the connection).
 */
class OutboundMailbox {
public:
  using CloseCallback = std::move_only_function<void()>;

  explicit OutboundMailbox(Connection& conn);

  void Enqueue(std::vector<std::byte> frame);
  auto RegisterOnClose(CloseCallback close_cb) -> std::size_t;
  void UnregisterOnClose(std::size_t token);

private:
  friend class Connection;
  void Close();

  Connection& conn_;
  bool active_{true};
  std::vector<std::pair<std::size_t, CloseCallback>> close_callbacks_;
  std::size_t next_token_{0};
};

} // namespace strij::io
