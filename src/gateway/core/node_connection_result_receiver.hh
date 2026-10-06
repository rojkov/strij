#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "common/core/io/outbound_mailbox.hh"
#include "common/task/task.pb.h"
#include "strij/gateway/result_receiver_storage.hh"

namespace strij::gateway {

// TLV sibling of HttpResultReceiver: a ResultReceiver that serializes outcomes
// back to a node connection. `Deliver` writes a kResult frame (TaskResult keyed
// by the fixed task id), `DeliverError` writes a kTaskRejected frame (keyed by
// the same id) — the node resolves both back to the original parent receiver
// via its LocalResultReceiverStorage. Bound to the submitting connection's mailbox
// so results return over the connection regardless of which gateway connection
// carried the original upstream submission.
class NodeConnectionResultReceiver final : public ResultReceiver {
public:
  NodeConnectionResultReceiver(std::string task_id, std::shared_ptr<io::OutboundMailbox> mailbox);

  void Deliver(std::span<const std::byte> value, bool is_final) override;
  // Writes a kTaskRejected frame. `status` has no representation there:
  // TaskRejected stays reason-only by design, because a rejection is an
  // admission outcome, not a task outcome. The code is accepted so every
  // receiver honours the one DeliverError contract.
  void DeliverError(std::string_view reason, task::TaskStatus status) override;

private:
  std::string task_id_;
  std::shared_ptr<io::OutboundMailbox> mailbox_;
};

} // namespace strij::gateway