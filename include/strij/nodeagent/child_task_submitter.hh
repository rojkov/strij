#pragma once

#include "common/task/task.pb.h"
#include "strij/common/pure.hh"
#include "strij/gateway/result_receiver_storage.hh"

namespace strij::nodeagent {

// The narrow public submission handle for child tasks, held by workflow
// (or any multi-step) task handlers. Obtained once at handler construction
// via NodeagentFactoryContext::ChildTaskSubmitter(); the concrete instance is
// the node-global NodeagentSchedulerRouter (a composite extensions::Scheduler).
//
// Submit() takes ownership of `receiver`; the child's result eventually
// resolves through it (either locally via the default scheduler's
// LocalResultReceiverStorage, or via the two-hop gateway path). The implementation
// dispatches by task_type to the appropriate local scheduler, which runs the
// child-policy step (DefaultLocalScheduler::Schedule).
class ChildTaskSubmitter {
public:
  ChildTaskSubmitter() = default;
  virtual ~ChildTaskSubmitter() = default;

  ChildTaskSubmitter(const ChildTaskSubmitter&) = delete;
  auto operator=(const ChildTaskSubmitter&) -> ChildTaskSubmitter& = delete;
  ChildTaskSubmitter(ChildTaskSubmitter&&) noexcept = delete;
  auto operator=(ChildTaskSubmitter&&) noexcept -> ChildTaskSubmitter& = delete;

  // Submits `task` as a child, taking ownership of `receiver`. The task's
  // type determines the routing through the node's scheduler authority
  // declarations. Ownership of `task` is taken (moved) so the implementation
  // can forward it without copying.
  virtual void Submit(task::Task task, gateway::ResultReceiverPtr receiver) PURE;
};

} // namespace strij::nodeagent
