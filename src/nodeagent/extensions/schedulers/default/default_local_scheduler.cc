#include "nodeagent/extensions/schedulers/default/default_local_scheduler.hh"

#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/logging/log.hh"
#include "common/task/task.pb.h"
#include "nodeagent/core/child_task_forwarder.hh"
#include "nodeagent/core/local_result_receiver_storage.hh"
#include "nodeagent/core/storage_result_sender.hh"
#include "nodeagent/extensions/schedulers/default/default.pb.h"
#include "strij/extensions/extension_registry.hh"
#include "strij/extensions/scheduler.hh"
#include "strij/gateway/result_receiver_storage.hh"
#include "strij/nodeagent/admission_controller.hh"
#include "strij/nodeagent/run_task_service.hh"

namespace strij::nodeagent::schedulers {

DefaultLocalScheduler::DefaultLocalScheduler(nodeagent::RunTaskService& run_task_service,
                                             nodeagent::AdmissionControllerSharedPtr admission,
                                             nodeagent::ChildTaskForwarder* forward)
    : run_task_service_{run_task_service}, admission_{std::move(admission)}, forward_{forward} {}

void DefaultLocalScheduler::Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) {
  // 1. Register home first so the receiver is observable (and resolvable) from
  //    the moment Schedule returns.
  const std::string& child_id = task.id();
  storage_.Put(child_id, std::move(receiver));

  // 2. No handler for the child's type: never admit — forward (or error).
  if (!run_task_service_.HasHandler(task.type())) {
    forwardOrError(task, child_id);
    return;
  }

  // 3. Try admission; on failure forward (or error), never queue.
  const absl::Status admit_status = admission_->Admit(task.type(), task.requirements());
  if (!admit_status.ok()) {
    LOG_DEBUG("Child task '{}' not admitted: {}", child_id, admit_status.message());
    forwardOrError(task, child_id);
    return;
  }

  // 4. Capacity reserved: run locally. The scope transfers to the tracking
  //    sender so a task that never reports a final result still releases
  //    capacity.
  auto scope = std::make_unique<AdmissionScope>(admission_, task.type(), task.requirements());
  auto sender = std::make_unique<StorageResultSender>(storage_, child_id);
  run_task_service_.RunTask(task, std::move(sender), std::move(scope));
}

void DefaultLocalScheduler::forwardOrError(const task::Task& task, const std::string& child_id) {
  if (forward_ != nullptr) {
    // Forward the child upstream. The receiver stays registered under child_id;
    // the child's outcome returns via the kResult / kTaskRejected frames this
    // scheduler owns and resolves this same entry.
    const absl::Status forward_status = forward_->Forward(task);
    if (forward_status.ok()) {
      return;
    }

    LOG_WARNING("Forwarding child task '{}' failed: {}", child_id, forward_status.message());
  }

  // No forward path (or unreachable gateway): the child cannot be satisfied
  // locally — resolve the receiver so the parent never hangs.
  if (gateway::ResultReceiver* receiver = storage_.Get(child_id); receiver != nullptr) {
    receiver->DeliverError("no local capacity and no gateway forward path is configured");
  }
  storage_.Erase(child_id);
}

auto DefaultLocalScheduler::HandleFrame(const io::TlvFrame& frame, io::Connection& /*conn*/)
    -> absl::Status {
  switch (frame.type_id) {
  case io::TlvFrame::kResult:
    return handleResultFrame(frame);
  case io::TlvFrame::kTaskRejected:
    return handleRejectedFrame(frame);
  default:
    return absl::NotFoundError("default scheduler owns only kResult / kTaskRejected frames");
  }
}

auto DefaultLocalScheduler::handleResultFrame(const io::TlvFrame& frame) -> absl::Status {
  task::TaskResult result;
  if (!result.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                             static_cast<int>(frame.value.size()))) {
    LOG_WARNING("Malformed TaskResult frame dropped");
    return absl::InvalidArgumentError("malformed TaskResult frame dropped");
  }

  gateway::ResultReceiver* receiver = storage_.Get(result.id());
  if (receiver == nullptr) {
    LOG_WARNING("Result for unknown child task '{}' dropped", result.id());
    return absl::OkStatus();
  }

  const bool is_final = !result.has_is_final() || result.is_final();
  const std::string& body = result.body();
  const auto data = std::as_bytes(std::span(body.data(), body.size()));
  receiver->Deliver(data, is_final);
  if (is_final) {
    storage_.Erase(result.id());
  }
  return absl::OkStatus();
}

auto DefaultLocalScheduler::handleRejectedFrame(const io::TlvFrame& frame) -> absl::Status {
  task::TaskRejected rejected;
  if (!rejected.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                               static_cast<int>(frame.value.size()))) {
    LOG_WARNING("Malformed TaskRejected frame dropped");
    return absl::InvalidArgumentError("malformed TaskRejected frame dropped");
  }

  gateway::ResultReceiver* receiver = storage_.Get(rejected.id());
  if (receiver == nullptr) {
    LOG_WARNING("Rejection for unknown child task '{}' dropped", rejected.id());
    return absl::OkStatus();
  }

  receiver->DeliverError(rejected.reason());
  storage_.Erase(rejected.id());
  return absl::OkStatus();
}

auto DefaultLocalScheduler::HandledFrameTypes() const -> std::span<const uint8_t> {
  static constexpr std::array<uint8_t, 2> kHandled = {io::TlvFrame::kResult,
                                                      io::TlvFrame::kTaskRejected};
  return kHandled;
}

auto DefaultLocalSchedulerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<extensions::schedulers::default_scheduler::DefaultSchedulerConfig>();
}

auto DefaultLocalSchedulerFactory::Create(const ::google::protobuf::Message& /*config*/,
                                          extensions::NodeagentFactoryContext& context)
    -> extensions::SchedulerPtr {
  return std::make_unique<DefaultLocalScheduler>(
      context.RunTaskService(), context.AdmissionController(), &context.ChildTaskForwarder());
}

} // namespace strij::nodeagent::schedulers

REGISTER_FACTORY_FULLY_QUALIFIED(strij::nodeagent::schedulers::DefaultLocalSchedulerFactory,
                                 strij::nodeagent::NodeSchedulerFactory,
                                 default_local_scheduler_registrar)