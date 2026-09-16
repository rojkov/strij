#pragma once

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "common/task/task.pb.h"
#include "nodeagent/core/local_receiver_registry.hh"
#include "strij/common/pure.hh"
#include "strij/gateway/result_receiver_storage.hh"
#include "strij/nodeagent/admission_controller.hh"
#include "strij/nodeagent/child_task_submitter.hh"
#include "strij/nodeagent/run_task_service.hh"

namespace strij::nodeagent {

// The outbound forward contract used by the child-policy step. Implemented by
// the node's GatewayClient (4B); before the forward path lands the step is
// constructed without one and delivery-deficient children error locally,
// preserving the Phase-0 behavior of a node that cannot enlist remote capacity.
class ChildForwarder {
public:
  ChildForwarder() = default;
  virtual ~ChildForwarder() = default;

  ChildForwarder(const ChildForwarder&) = delete;
  auto operator=(const ChildForwarder&) -> ChildForwarder& = delete;
  ChildForwarder(ChildForwarder&&) noexcept = delete;
  auto operator=(ChildForwarder&&) noexcept -> ChildForwarder& = delete;

  // Forwards `task` to a gateway by writing an upstream kTaskSubmission frame.
  // Ownership of the receiver is NOT taken here — it stays registered in the
  // LocalReceiverRegistry under task.id(), and the child's outcome returns
  // through the registry (via the core kResult/kTaskRejected cases).
  // Returns OkStatus on success (receiver stays in registry for async
  // resolution). Returns a non-Ok status when forwarding is impossible (no
  // live connection); the caller delivers an error and erases the registry
  // entry so the parent never hangs.
  virtual auto Forward(const task::Task& task) -> absl::Status PURE;
};

// The shared child-policy step invoked by every local scheduler's Schedule
// facet (D3):
//
//   1. Register the child's receiver in the local registry, keyed by child id
//      ("parent registers a child receiver before Submit" — the registration
//      is observable by Get() as soon as Submit returns).
//   2. If no handler is registered for the child's type → forward (or error).
//   3. Attempt AdmissionController::Admit; on success run the child locally
//      via the sender-backed, preallocated RunTask with a RegistryResultSender
//      (the held scope transfers to the tracking sender; no double-admit).
//   4. On admission failure (capacity/requirements) → forward (or error).
//
// The registry entry created in (1) is erased on the child's final result or
// rejection (RegistryResultSender), or directly after a locally-delivered
// forward error. A *forwarded* child's outcome returns via the
// NodeagentTlvHandler core kResult/kTaskRejected cases, which resolve the same
// registry entry — the parent cannot distinguish local from remote execution.
class ChildSubmissionService final : public ChildTaskSubmitter {
public:
  explicit ChildSubmissionService(LocalReceiverRegistry& registry,
                                  RunTaskService& run_task_service,
                                  AdmissionControllerSharedPtr admission,
                                  ChildForwarder* forward = nullptr);

  ChildSubmissionService(const ChildSubmissionService&) = delete;
  auto operator=(const ChildSubmissionService&) -> ChildSubmissionService& = delete;
  ChildSubmissionService(ChildSubmissionService&&) noexcept = delete;
  auto operator=(ChildSubmissionService&&) noexcept -> ChildSubmissionService& = delete;

  // ChildTaskSubmitter
  void Submit(task::Task task, gateway::ResultReceiverPtr receiver) override;

private:
  // Forwards via the configured ChildForwarder (4B); with none, delivers an
  // error to the registered receiver and erases the entry.
  void forwardOrError(const task::Task& task, const std::string& child_id);

  LocalReceiverRegistry& registry_;
  RunTaskService& run_task_service_;
  AdmissionControllerSharedPtr admission_;
  ChildForwarder* forward_;
};

} // namespace strij::nodeagent