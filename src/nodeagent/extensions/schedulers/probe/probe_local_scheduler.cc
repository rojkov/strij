#include "nodeagent/extensions/schedulers/probe/probe_local_scheduler.hh"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/logging/log.hh"
#include "common/task/probe.pb.h"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/schedulers/probe/probe.pb.h"
#include "strij/extensions/extension_registry.hh"
#include "strij/extensions/scheduler.hh"
#include "strij/gateway/result_receiver_storage.hh"
#include "strij/nodeagent/run_task_service.hh"

namespace strij::nodeagent::schedulers::probe {

namespace {

// Fallbacks when the typed config omits the fields. Absent queue_capacity
// means "a queue exists at a sane size"; the overbooking dial defaults low so
// the walk never preallocates an entire backlog in one release pass.
constexpr size_t kDefaultQueueCapacity = 64;
constexpr size_t kDefaultMaxPreallocations = 4;

auto parseMessage(const io::TlvFrame& frame, google::protobuf::Message& message) -> bool {
  return message.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                                static_cast<int>(frame.value.size()));
}

} // namespace

ProbeLocalScheduler::ProbeLocalScheduler(nodeagent::RunTaskService& run_task_service,
                                         nodeagent::AdmissionControllerSharedPtr admission,
                                         nodeagent::DataDependencyFetcherRouter& router,
                                         event::Dispatcher& dispatcher, size_t queue_capacity,
                                         size_t max_concurrent_preallocations)
    : run_task_service_{run_task_service}, admission_{std::move(admission)}, router_{router},
      dispatcher_{dispatcher}, max_concurrent_preallocations_{max_concurrent_preallocations},
      queue_{queue_capacity} {
  admission_->RegisterCapacityObserver(this);
}

void ProbeLocalScheduler::Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) {
  // The probe entry is a pure wire-protocol counterpart: it never runs
  // locally-originated children (that is the bundled "default" scheduler's
  // job). Honor the resolve contract so a misrouted child can never hang its
  // parent.
  LOG_WARNING("Unimplemented: probe local scheduler cannot run child task '{}' of type '{}' "
              "(configure the \"default\" scheduler as the local authority)",
              task.id(), task.type());
  receiver->DeliverError(
      "unimplemented: configure the \"default\" scheduler as the local authority for child tasks");
}

auto ProbeLocalScheduler::RequiredProtocol() const -> std::string_view { return "probe"; }

void ProbeLocalScheduler::sendDecline(io::OutboundMailbox& mailbox, const std::string& task_id,
                                      std::string_view reason) {
  task::TaskDecline decline;
  decline.set_id(task_id);
  decline.set_reason(std::string(reason));

  std::string serialized;
  decline.SerializeToString(&serialized);
  const auto data = std::as_bytes(std::span(serialized.data(), serialized.size()));
  mailbox.Enqueue(io::SerializeTlvFrame(io::TlvFrame::kTaskDecline, data));

  LOG_DEBUG("Task {} declined: {}", task_id, reason);
}

void ProbeLocalScheduler::walk() {
  std::vector<QueuedProbe> deferred;
  for (;;) {
    // The overbooking dial: never preallocate more than the configured cap in
    // one walk pass (or while the previous walk's preallocations are pending).
    if (pull_pending_.size() >= max_concurrent_preallocations_) {
      break;
    }

    auto item = queue_.TryPop();
    if (!item) {
      break;
    }

    // Readiness gate: only pull a queued probe once its deps are all cached.
    // Deps are prefetched on enqueue; a probe sitting in the queue for a
    // later release must not burn freed capacity before its data arrives.
    // Empty deps are "all cached" by definition (Phase-2 behavior). Refs whose
    // scheme has no registered fetcher never gate readiness (AllCached), so a
    // node with a partial fetcher set still runs the task.
    if (!router_.AllCached(item->probe.deps())) {
      deferred.push_back(std::move(*item));
      continue;
    }

    const absl::Status status = admission_->Admit(item->probe.type(), item->probe.requirements());
    if (status.ok()) {
      auto scope = std::make_unique<AdmissionScope>(admission_, item->probe.type(),
                                                    item->probe.requirements());
      const std::string task_id = item->probe.id();
      pull_pending_.insert_or_assign(
          task_id, PullPending{.probe = std::move(item->probe), .scope = std::move(scope)});

      // Pull via the retained mailbox (D9): a no-op on stale connections.
      task::TaskPull pull;
      pull.set_id(task_id);
      std::string serialized;
      pull.SerializeToString(&serialized);
      const auto data = std::as_bytes(std::span(serialized.data(), serialized.size()));
      item->pull_mailbox->Enqueue(io::SerializeTlvFrame(io::TlvFrame::kTaskPull, data));
    } else if (absl::IsFailedPrecondition(status)) {
      // Never satisfiable (undeclared pool / requirements exceed a pool): a
      // queue corpse — decline and drop it rather than stall FIFO behind it.
      sendDecline(*item->pull_mailbox, item->probe.id(), "requirements unsatisfiable");
    } else {
      // ResourceExhausted: still doesn't fit; keep it queued in FIFO order.
      deferred.push_back(std::move(*item));
    }
  }

  for (auto& item : deferred) {
    queue_.Push(std::move(item));
  }
}

void ProbeLocalScheduler::startPrefetch(const task::TaskProbe& probe) {
  // Empty deps = no prefetch (Phase-2 behavior). Fully-cached deps are skipped
  // too: the byte store is node-global, so an already-present ref needs neither
  // a fetch nor the DEP_COMPLETED traffic it would generate. Use the router's
  // readiness predicate so refs for schemes with no registered fetcher are
  // treated the same here as in walk() (they never gate readiness).
  if (probe.deps().empty() || router_.AllCached(probe.deps())) {
    return;
  }
  router_.FetchAll(probe.deps(), probe.id(), dispatcher_, this);
}

void ProbeLocalScheduler::handleProbe(const task::TaskProbe& probe,
                                      const std::shared_ptr<io::OutboundMailbox>& pull_mailbox) {
  const absl::Status status = admission_->Admit(probe.type(), probe.requirements());
  if (status.ok()) {
    // Free capacity: preallocate a slot and claim the task.
    auto scope = std::make_unique<AdmissionScope>(admission_, probe.type(), probe.requirements());
    pull_pending_.insert_or_assign(probe.id(),
                                   PullPending{.probe = probe, .scope = std::move(scope)});

    task::TaskPull pull;
    pull.set_id(probe.id());
    std::string serialized;
    pull.SerializeToString(&serialized);
    const auto data = std::as_bytes(std::span(serialized.data(), serialized.size()));
    pull_mailbox->Enqueue(io::SerializeTlvFrame(io::TlvFrame::kTaskPull, data));

    startPrefetch(probe);
    return;
  }

  if (!absl::IsResourceExhausted(status)) {
    // Permanent refusal: never enqueue — decline immediately.
    sendDecline(*pull_mailbox, probe.id(), "requirements unsatisfiable");
    return;
  }

  if (!queue_.Push(QueuedProbe{.probe = probe, .pull_mailbox = pull_mailbox})) {
    // Pull_mailbox is still valid (shared_ptr was copied, not moved): the
    // decline can still go out on the connection that sent the probe.
    sendDecline(*pull_mailbox, probe.id(), "queue full");
    return;
  }

  startPrefetch(probe);
}

auto ProbeLocalScheduler::removeQueued(const std::string& task_id) -> bool {
  std::vector<QueuedProbe> kept;
  bool found = false;
  while (auto item = queue_.TryPop()) {
    if (item->probe.id() != task_id) {
      kept.push_back(std::move(*item));
    } else {
      found = true;
    }
  }
  for (auto& item : kept) {
    queue_.Push(std::move(item));
  }
  return found;
}

void ProbeLocalScheduler::ProcessCommand(event::Command cmd) {
  // Pure wakeup: capacity may have been released; re-derive from the
  // AdmissionController and drain what now fits.
  if (cmd.type_ == event::Command::CAPACITY_RELEASED) {
    walk();
    return;
  }

  if (cmd.type_ == event::Command::DEP_COMPLETED) {
    // A data dependency finished fetching. args_ points to a stable task id the
    // router keeps alive past command delivery. The id is advisory: walk()
    // re-evaluates every queued probe, so a task whose deps are now cached gets
    // pulled. A task that already resolved or was cancelled is simply absent from
    // the queue/pending maps — walk() finds nothing for it, i.e. a no-op.
    const auto* task_id = static_cast<const std::string*>(cmd.args_);
    if (task_id != nullptr) {
      LOG_DEBUG("DEP_COMPLETED for task {}; re-evaluating readiness", *task_id);
    }
    walk();
    return;
  }
}

auto ProbeLocalScheduler::HandleFrame(const io::TlvFrame& frame, io::Connection& conn)
    -> absl::Status {
  switch (frame.type_id) {
  case io::TlvFrame::kTaskProbe: {
    task::TaskProbe probe;
    if (!parseMessage(frame, probe)) {
      LOG_WARNING("Malformed TaskProbe frame dropped");
      return absl::InvalidArgumentError("malformed TaskProbe frame dropped");
    }
    walk();
    handleProbe(probe, conn.Mailbox());
    return absl::OkStatus();
  }
  case io::TlvFrame::kTaskGrant: {
    task::Task task;
    if (!parseMessage(frame, task)) {
      LOG_WARNING("Malformed Task frame (grant) dropped");
      return absl::InvalidArgumentError("malformed Task frame dropped");
    }

    auto iter = pull_pending_.find(task.id());
    if (iter == pull_pending_.end()) {
      LOG_WARNING("Grant for task '{}' with no held reservation; dropped", task.id());
      return absl::OkStatus();
    }

    PullPending pending = std::move(iter->second);
    pull_pending_.erase(iter);
    // The held preallocation transfers to the result sender: no second Admit.
    run_task_service_.RunTask(task, conn, std::move(pending.scope));
    return absl::OkStatus();
  }
  case io::TlvFrame::kTaskProbeCancel: {
    task::TaskProbeCancel cancel;
    if (!parseMessage(frame, cancel)) {
      LOG_WARNING("Malformed TaskProbeCancel frame dropped");
      return absl::InvalidArgumentError("malformed TaskProbeCancel frame dropped");
    }

    // Erasing the pull-pending scope releases the preallocation via its
    // destructor → CAPACITY_RELEASED → walk(). Idempotent for unknown ids.
    const size_t erased = pull_pending_.erase(cancel.id());
    const bool removed_from_queue = removeQueued(cancel.id());
    if (erased + (removed_from_queue ? 1U : 0U) == 0U) {
      LOG_DEBUG("Cancel for unknown task '{}' ignored", cancel.id());
    }
    return absl::OkStatus();
  }
  default:
    return absl::NotFoundError("probe scheduler does not own TLV type_id " +
                               std::to_string(frame.type_id));
  }
}

auto ProbeLocalScheduler::HandledFrameTypes() const -> std::span<const uint8_t> {
  static constexpr std::array<uint8_t, 3> kTypes = {
      io::TlvFrame::kTaskProbe, io::TlvFrame::kTaskProbeCancel, io::TlvFrame::kTaskGrant};
  return kTypes;
}

auto ProbeLocalSchedulerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<extensions::schedulers::probe::ProbeSchedulerConfig>();
}

auto ProbeLocalSchedulerFactory::Create(const ::google::protobuf::Message& config,
                                        extensions::NodeagentFactoryContext& context)
    -> extensions::SchedulerPtr {
  const auto* typed =
      dynamic_cast<const extensions::schedulers::probe::ProbeSchedulerConfig*>(&config);
  if (typed == nullptr) {
    LOG_ERROR("ProbeLocalSchedulerFactory: typed_config is not a ProbeSchedulerConfig");
    return nullptr;
  }

  const size_t queue_capacity =
      typed->has_queue_capacity() ? typed->queue_capacity() : kDefaultQueueCapacity;
  const size_t max_preallocations = typed->has_max_concurrent_preallocations()
                                        ? typed->max_concurrent_preallocations()
                                        : kDefaultMaxPreallocations;
  if (queue_capacity == 0U || max_preallocations == 0U) {
    LOG_ERROR("ProbeLocalSchedulerConfig: queue_capacity and max_concurrent_preallocations "
              "must be >= 1");
    return nullptr;
  }

  return std::make_unique<ProbeLocalScheduler>(
      context.RunTaskService(), context.AdmissionController(),
      context.DataDependencyFetcherRouter(), context.Dispatcher(), queue_capacity,
      max_preallocations);
}

} // namespace strij::nodeagent::schedulers::probe

REGISTER_FACTORY_FULLY_QUALIFIED(strij::nodeagent::schedulers::probe::ProbeLocalSchedulerFactory,
                                 strij::nodeagent::NodeSchedulerFactory,
                                 probe_local_scheduler_registrar)