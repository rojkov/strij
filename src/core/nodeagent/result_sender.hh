#pragma once

#include "core/io/connection.hh"
#include "extensions/task_handlers/task_handlers.hh"

namespace carrot::nodeagent {

/**
 * @brief Binds a ResultSender to a live Connection for synchronous delivery.
 *
 * SAFETY: holds a raw Connection&. It is only safe to call Send() while the
 * connection is alive, which today holds only inside
 * NodeagentTlvHandler::HandleFrame on the event-loop thread (the connection
 * is owned by TcpListener and cannot be torn down mid-call). Senders must
 * therefore never outlive the HandleTask() invocation they were created for.
 *
 * Async delivery (holding a sender past HandleTask) requires moving writes to
 * a connection-owned mailbox that is dropped on close; that is a separate
 * change.
 */
class ConnectionResultSender final : public carrot::extensions::ResultSender {
public:
  explicit ConnectionResultSender(carrot::io::Connection& conn);

  void Send(carrot::task::TaskResult result) override;

private:
  carrot::io::Connection& conn_;
};

} // namespace carrot::nodeagent
