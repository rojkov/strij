#pragma once

#include "absl/status/status.h"
#include "common/task/task.pb.h"
#include "strij/common/pure.hh"

namespace strij::nodeagent {

// The outbound forward contract used by the node's local-authority scheduler
// (the bundled "default" scheduler's child-policy step). Implemented by the
// node's GatewayClient: forwards a child that cannot be satisfied locally to a
// gateway over a live connection.
class ChildTaskForwarder {
public:
  ChildTaskForwarder() = default;
  virtual ~ChildTaskForwarder() = default;

  ChildTaskForwarder(const ChildTaskForwarder&) = delete;
  auto operator=(const ChildTaskForwarder&) -> ChildTaskForwarder& = delete;
  ChildTaskForwarder(ChildTaskForwarder&&) noexcept = delete;
  auto operator=(ChildTaskForwarder&&) noexcept -> ChildTaskForwarder& = delete;

  // Forwards `task` to a gateway by writing an upstream kTaskSubmission frame.
  // Ownership of the receiver is NOT taken here — it stays registered in the
  // caller's LocalResultReceiverStorage under task.id(), and the child's outcome
  // returns through the storage (via the bundled "default" scheduler's owned
  // kResult/kTaskRejected frame handling).
  // Returns OkStatus on success (receiver stays in storage for async
  // resolution). Returns a non-Ok status when forwarding is impossible (no
  // live connection); the caller delivers an error and erases the storage
  // entry so the parent never hangs.
  virtual auto Forward(const task::Task& task) -> absl::Status PURE;
};

} // namespace strij::nodeagent