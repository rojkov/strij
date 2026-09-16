#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/task/task.pb.h"
#include "nodeagent/config/nodeagent.pb.h"
#include "strij/extensions/scheduler.hh"
#include "strij/nodeagent/child_task_submitter.hh"

namespace strij::nodeagent {

// Node-side composite of the configured local schedulers, and the single child
// submission handle a workflow handler holds (via
// NodeagentFactoryContext::ChildTaskSubmitter()).
//
// Submit()/Schedule() dispatches a child task to the entry that declares its
// type via NodeSchedulerConfig.task_type, falling back to the single entry
// declared with local_default=true. A child whose type is claimed by no entry
// and with no local default declared is delivered an error through its
// receiver. Entries with neither role declaration never receive child
// submissions here — they are pure wire-protocol counterparts and appear only
// in the frame dispatcher.
//
// The router itself never registers receivers; per-task registration happens
// in the owning local scheduler's child-policy step
// (ChildSubmissionService::Submit).
class ChildSchedulerRouter final : public extensions::Scheduler, public ChildTaskSubmitter {
public:
  struct ChildRoutedScheduler {
    extensions::SchedulerPtr scheduler;
    std::string task_type;  // optional local authority; empty = not a local role
    bool local_default{false};
  };

  // `schedulers` must be non-empty, with at most one local_default entry and
  // unique non-empty task_type bindings (validated by BuildChildSchedulerRouter).
  explicit ChildSchedulerRouter(std::vector<ChildRoutedScheduler> schedulers);
  ~ChildSchedulerRouter() override = default;

  ChildSchedulerRouter(const ChildSchedulerRouter&) = delete;
  auto operator=(const ChildSchedulerRouter&) -> ChildSchedulerRouter& = delete;
  ChildSchedulerRouter(ChildSchedulerRouter&&) noexcept = delete;
  auto operator=(ChildSchedulerRouter&&) noexcept -> ChildSchedulerRouter& = delete;

  // ChildTaskSubmitter
  void Submit(task::Task task, gateway::ResultReceiverPtr receiver) override;

  // extensions::Scheduler (the submission facet; the router owns no frames)
  void Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) override;
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override;
  auto HandleFrame(const io::TlvFrame& /*frame*/, io::Connection& /*conn*/) -> absl::Status override {
    return absl::NotFoundError("child router owns no frame types");
  }
  [[nodiscard]] auto HandledFrameTypes() const -> std::span<const uint8_t> override;

  [[nodiscard]] auto RoutedSchedulerCount() const -> size_t { return schedulers_.size(); }

  // Raw pointers to the owned constituent schedulers, for the inbound frame
  // dispatcher (NodeagentTlvHandler): each constituent's HandledFrameTypes()
  // claims the node's scheduling frame types. The pointers stay valid for the
  // router's lifetime. Not const: the span exposes mutable element pointers.
  [[nodiscard]] auto LocalSchedulerPointers() -> std::span<extensions::Scheduler*>;

private:
  auto findChildScheduler(const task::Task& task) -> extensions::Scheduler*;

  std::vector<ChildRoutedScheduler> schedulers_;
  std::vector<extensions::Scheduler*> scheduler_pointers_;
  // Determined from the first constituent with a non-empty RequiredProtocol();
  // informational only (the router never advertises a protocol itself).
  std::string_view required_protocol_;
};

// Loads one scheduler per NodeAgentConfig.schedulers entry via
// CreateNodeScheduler and composes them into a router, applying the node-side
// authority doctrine (D2): an empty task_type is NOT a default, at most one
// local_default entry, unique non-empty task_type claims, and no-role entries
// never receive child submissions. Fails on an empty list, an unknown
// scheduler name, or ambiguous authority declarations.
auto BuildChildSchedulerRouter(const config::NodeAgentConfig& config,
                               extensions::NodeagentFactoryContext& context)
    -> absl::StatusOr<std::unique_ptr<ChildSchedulerRouter>>;

} // namespace strij::nodeagent