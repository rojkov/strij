#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "absl/status/status.h"
#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/task/task.pb.h"
#include "nodeagent/core/local_result_receiver_storage.hh"
#include "strij/extensions/scheduler.hh"
#include "strij/gateway/result_receiver_storage.hh"
#include "strij/nodeagent/admission_controller.hh"
#include "strij/nodeagent/child_task_forwarder.hh"
#include "strij/nodeagent/run_task_service.hh"

namespace strij::nodeagent::schedulers {

// The bundled local-authority scheduler (config name "default"). This is the
// node's local_default authority: the ONLY local scheduler with a functional
// Schedule facet. It owns the whole child-policy step:
//
//   1. Register the child's receiver in its own LocalResultReceiverStorage, keyed by
//      child id (the registration is observable from the moment Schedule
//      returns).
//   2. If no handler is registered for the child's type → forward (or error).
//   3. Attempt AdmissionController::Admit; on success run the child locally via
//      the sender-backed, preallocated RunTask with a StorageResultSender (the
//      held scope transfers to the tracking sender; no double-admit).
//   4. On admission failure (capacity/requirements) → forward (or error).
//
// The storage entry created in (1) is erased on the child's final result or
// rejection (StorageResultSender), or directly after a locally-delivered
// forward error. A *forwarded* child's outcome returns via the kResult /
// kTaskRejected frames this scheduler owns in the frame dispatcher, which
// resolve the same storage entry — the parent cannot distinguish local from
// remote execution. The child result-receiver storage is fully private to this
// scheduler; no other component (handler, router, dispatcher) holds a ref.
//
// The scheduler claims no wire protocol (RequiredProtocol() is empty): it
// exists purely as the local authority, so a node configured without it is a
// degenerate "wire-only" node that drops inbound child-outcome frames and
// errors all locally-originated children.
class DefaultLocalScheduler final : public extensions::Scheduler {
public:
  // `forward` (optional) is the node's outbound path (the GatewayClient);
  // without one, delivered-deficient children error locally.
  DefaultLocalScheduler(nodeagent::RunTaskService& run_task_service,
                        nodeagent::AdmissionControllerSharedPtr admission,
                        nodeagent::ChildTaskForwarder* forward);
  ~DefaultLocalScheduler() override = default;

  DefaultLocalScheduler(const DefaultLocalScheduler&) = delete;
  auto operator=(const DefaultLocalScheduler&) -> DefaultLocalScheduler& = delete;
  DefaultLocalScheduler(DefaultLocalScheduler&&) noexcept = delete;
  auto operator=(DefaultLocalScheduler&&) noexcept -> DefaultLocalScheduler& = delete;

  // extensions::Scheduler
  void Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) override;
  // Advertises no scheduling protocol: this is the local authority, not a wire
  // counterpart.
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override { return {}; }
  // Owns the child-outcome frames (kResult / kTaskRejected): resolves the
  // registered receiver, erasing the entry on terminal outcomes.
  auto HandleFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status override;
  [[nodiscard]] auto HandledFrameTypes() const -> std::span<const uint8_t> override;

  // The child result-receiver storage owned by this scheduler — the only
  // component that registers or erases child receivers. Exposed for the
  // framework's verification paths and tests; production frame resolution goes
  // through HandleFrame, never through the storage directly.
  [[nodiscard]] auto Storage() -> LocalResultReceiverStorage& { return storage_; }

private:
  auto handleResultFrame(const io::TlvFrame& frame) -> absl::Status;
  auto handleRejectedFrame(const io::TlvFrame& frame) -> absl::Status;
  // Forwards via the configured ChildTaskForwarder; with none (or a failed
  // forward), delivers an error to the registered receiver and erases the
  // entry so the parent never hangs.
  void forwardOrError(const task::Task& task, const std::string& child_id);

  nodeagent::RunTaskService& run_task_service_;
  nodeagent::AdmissionControllerSharedPtr admission_;
  nodeagent::ChildTaskForwarder* forward_;

  LocalResultReceiverStorage storage_;
};

class DefaultLocalSchedulerFactory final : public NodeSchedulerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override { return "default"; }
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override { return {}; }
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, const NodeSchedulerDeps& deps)
      -> extensions::SchedulerPtr override;
};

} // namespace strij::nodeagent::schedulers