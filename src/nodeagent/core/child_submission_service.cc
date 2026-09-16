#include "nodeagent/core/child_submission_service.hh"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "common/core/logging/log.hh"
#include "common/task/task.pb.h"
#include "nodeagent/core/local_receiver_registry.hh"
#include "nodeagent/core/registry_result_sender.hh"
#include "strij/gateway/result_receiver_storage.hh"
#include "strij/nodeagent/admission_controller.hh"
#include "strij/nodeagent/run_task_service.hh"

namespace strij::nodeagent {

ChildSubmissionService::ChildSubmissionService(LocalReceiverRegistry& registry,
                                               RunTaskService& run_task_service,
                                               AdmissionControllerSharedPtr admission,
                                               ChildForwarder* forward)
    : registry_{registry}, run_task_service_{run_task_service},
      admission_{std::move(admission)}, forward_{forward} {}

void ChildSubmissionService::Submit(task::Task task, gateway::ResultReceiverPtr receiver) {
  const std::string child_id = task.id();
  registry_.Put(child_id, std::move(receiver));

  if (!run_task_service_.HasHandler(task.type())) {
    forwardOrError(task, child_id);
    return;
  }

  const absl::Status admit_status = admission_->Admit(task.type(), task.requirements());
  if (!admit_status.ok()) {
    LOG_DEBUG("Child task '{}' not admitted: {}", child_id, admit_status.message());
    forwardOrError(task, child_id);
    return;
  }

  // Capacity reserved: run locally. The scope transfers to the tracking sender
  // so a task that never reports a final result still releases capacity.
  auto scope = std::make_unique<AdmissionScope>(admission_, task.type(), task.requirements());
  auto sender = std::make_unique<RegistryResultSender>(registry_, child_id);
  run_task_service_.RunTask(task, std::move(sender), std::move(scope));
}

void ChildSubmissionService::forwardOrError(const task::Task& task, const std::string& child_id) {
  if (forward_ != nullptr) {
    // Forward the child upstream. The receiver stays registered under child_id;
    // the child's outcome returns via the core kResult/kTaskRejected cases and
    // resolves this same entry.
    const absl::Status forward_status = forward_->Forward(task);
    if (forward_status.ok()) {
      return;
    }

    LOG_WARNING("Forwarding child task '{}' failed: {}", child_id, forward_status.message());
  }

  // No forward path (4A / unreachable gateway): the child cannot be satisfied
  // locally — resolve the receiver so the parent never hangs.
  if (gateway::ResultReceiver* receiver = registry_.Get(child_id); receiver != nullptr) {
    receiver->DeliverError("no local capacity and no gateway forward path is configured");
  }
  registry_.Erase(child_id);
}

} // namespace strij::nodeagent