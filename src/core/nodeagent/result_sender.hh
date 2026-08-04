#pragma once

#include <cstddef>
#include <functional>
#include <memory>

#include "core/io/outbound_mailbox.hh"
#include "extensions/task_handlers/task_handlers.hh"

namespace strij::nodeagent {

/**
 * @brief ResultSender bound to a Connection's outbound mailbox.
 *
 * Holds a shared_ptr to the connection-owned OutboundMailbox, so a copy may be
 * retained past HandleTask() and past the connection's lifetime. Sends after
 * the connection is torn down are dropped (the mailbox is closed); the
 * lifecycle hooks forward to the mailbox.
 */
class ConnectionResultSender final : public extensions::ResultSender {
public:
  explicit ConnectionResultSender(std::shared_ptr<io::OutboundMailbox> mailbox);

  void Send(task::TaskResult result) override;
  auto RegisterOnClose(std::move_only_function<void()> close_cb) -> std::size_t override;
  void UnregisterOnClose(std::size_t token) override;

private:
  std::shared_ptr<strij::io::OutboundMailbox> mailbox_;
};

} // namespace strij::nodeagent
