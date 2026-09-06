#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "common/core/io/connection.hh"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/utils/bounded_queue.hh"
#include "common/task/probe.pb.h"
#include "common/task/task.pb.h"
#include "strij/event/command.hh"
#include "strij/event/command_handler.hh"
#include "strij/extensions/scheduler.hh"
#include "strij/nodeagent/admission_controller.hh"
#include "strij/nodeagent/run_task_service.hh"

namespace strij::nodeagent::schedulers::probe {

// The nodeagent half of the probe scheduling protocol (Sparrow-style). The
// gateway probes a small candidate set on a node's live capacity; this
// scheduler owns the kTaskProbe / kTaskProbeCancel / kTaskGrant frame types
// in the nodeagent frame dispatcher and implements deferred admission:
//
//   - kTaskProbe arrives → walk the queue, then Admit-or-Enqueue
//     (admit ok → preallocate + kTaskPull; ResourceExhausted → enqueue, or
//     kTaskDecline "queue full" when the bounded queue is full;
//     FailedPrecondition → kTaskDecline "requirements unsatisfiable").
//   - CAPACITY_RELEASED → walk the queue FIFO, preallocating what fits up to
//     the per-walk concurrency cap. Pulls go out on the mailbox retained with
//     the queued probe (D9), so a stale connection can never be written.
//   - kTaskGrant → run via the scope-carrying RunTask variant with the held
//     preallocation (no double admit); a grant for an id with no held
//     reservation is logged and dropped.
//   - kTaskProbeCancel → release the held preallocation and/or drop the queued
//     entry; idempotent for unknown ids.
//
// Registered as a capacity observer at construction (AdmissionController::RegisterCapacityObserver),
// so every release — completion, decline, cancel — re-triggers the queue walk.
class ProbeLocalScheduler final : public extensions::Scheduler,
                                  public event::CommandHandler {
public:
  ProbeLocalScheduler(nodeagent::RunTaskService& run_task_service,
                      nodeagent::AdmissionControllerSharedPtr admission,
                      size_t queue_capacity, size_t max_concurrent_preallocations);
  ~ProbeLocalScheduler() override = default;

  ProbeLocalScheduler(const ProbeLocalScheduler&) = delete;
  auto operator=(const ProbeLocalScheduler&) -> ProbeLocalScheduler& = delete;
  ProbeLocalScheduler(ProbeLocalScheduler&&) noexcept = delete;
  auto operator=(ProbeLocalScheduler&&) noexcept -> ProbeLocalScheduler& = delete;

  // extensions::Scheduler
  void Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) override;
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override;
  auto HandleFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status override;
  [[nodiscard]] auto HandledFrameTypes() const -> std::span<const uint8_t> override;

  // event::CommandHandler
  void ProcessCommand(event::Command cmd) override;

private:
  // A probe awaiting freed capacity. Retains the connection's mailbox so a
  // later release-walk can send the kTaskPull even if the connection changed
  // state; writes to closed mailboxes are no-ops.
  struct QueuedProbe {
    task::TaskProbe probe;
    std::shared_ptr<io::OutboundMailbox> pull_mailbox;
  };

  // A probe whose admission was preallocated and whose kTaskPull was sent.
  struct PullPending {
    task::TaskProbe probe;
    nodeagent::AdmissionScopePtr scope;
  };

  // Drains the queue oldest-first, preallocating (and pulling) every queued
  // probe that now fits, up to max_concurrent_preallocations. Probes that still
  // don't fit stay queued in FIFO order; probes that now fail a precondition
  // are declined and dropped.
  void walk();
  // Admit-or-enqueue the arriving probe per the decision tree.
  void handleProbe(const task::TaskProbe& probe,
                   std::shared_ptr<io::OutboundMailbox> pull_mailbox);
  // Removes the queued probe for `id` (if any), preserving FIFO order.
  auto removeQueued(const std::string& id) -> bool;
  // Serializes and writes a kTaskDecline on `mailbox`.
  static void sendDecline(io::OutboundMailbox& mailbox, const std::string& id,
                          std::string_view reason);

  nodeagent::RunTaskService& run_task_service_;
  nodeagent::AdmissionControllerSharedPtr admission_;
  size_t max_concurrent_preallocations_;
  utils::BoundedQueue<QueuedProbe> queue_;
  std::unordered_map<std::string, PullPending> pull_pending_;
};

class ProbeLocalSchedulerFactory final : public NodeSchedulerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override { return "probe"; }
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override { return "probe"; }
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config,
              extensions::NodeagentFactoryContext& context) -> extensions::SchedulerPtr override;
};

} // namespace strij::nodeagent::schedulers::probe