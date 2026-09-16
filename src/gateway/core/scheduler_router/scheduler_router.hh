#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/task/task.pb.h"
#include "gateway/config/gateway.pb.h"
#include "strij/extensions/factory_context.hh"
#include "strij/extensions/scheduler.hh"

namespace strij::gateway {

class ResultReceiverStorage;

// Composite gateway extensions::Scheduler built from GatewayConfig.schedulers. Schedule()
// routes to the scheduler bound to task.type() via SchedulerConfig.task_type,
// falling back to the entry with an empty task_type (the default). A task whose
// type matches neither is delivered an error through its receiver. Owns its
// constituent schedules; configured per-instance, never registered as an
// extension itself.
//
// The router also claims the kTaskSubmission frame type on gateway Node
// connections (the upstream child-forward path): an inbound submission is
// parsed, a NodeConnectionResultReceiver is stored in the ResultReceiverStorage
// keyed by the child's id and the submitting node's id, and the task is then
// dispatched through the normal per-type Schedule routing. Keying the entry by
// the *submitting* node lets the existing disconnect cleanup unwind a node's
// outstanding forwarded children.
class SchedulerRouter final : public extensions::Scheduler {
public:
  struct RoutedScheduler {
    extensions::SchedulerPtr scheduler;
    std::string task_type; // empty = default
  };

  // `schedulers` must be non-empty, with at most one default (empty task_type)
  // and unique non-empty task_type bindings.
  SchedulerRouter(std::vector<RoutedScheduler> schedulers, gateway::ResultReceiverStorage& storage);
  ~SchedulerRouter() override = default;

  SchedulerRouter(const SchedulerRouter&) = delete;
  auto operator=(const SchedulerRouter&) -> SchedulerRouter& = delete;
  SchedulerRouter(SchedulerRouter&&) noexcept = delete;
  auto operator=(SchedulerRouter&&) noexcept -> SchedulerRouter& = delete;

  void Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) override;
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override;
  auto HandleFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status override;
  [[nodiscard]] auto HandledFrameTypes() const -> std::span<const uint8_t> override;

  [[nodiscard]] auto RoutedSchedulerCount() const -> size_t { return schedulers_.size(); }

private:
  // Handles an inbound upstream kTaskSubmission (a node forwarding a child
  // upstream): stores a node-connection receiver keyed by the submitting node
  // and routes the task through the normal per-type dispatch.
  auto handleChildSubmission(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status;

  auto findSchedulerFor(const task::Task& task) -> extensions::Scheduler*;
  auto findFrameOwner(uint8_t type_id) -> extensions::Scheduler*;

  std::vector<RoutedScheduler> schedulers_;
  // Union of the constituents' HandledFrameTypes() (deduplicated) plus the
  // router-owned kTaskSubmission, owned so HandledFrameTypes() has a stable span.
  std::vector<uint8_t> handled_types_;
  // One of the constituents declaring RequiredProtocol(); the composite routes
  // only via this protocol.
  std::string_view required_protocol_;
  gateway::ResultReceiverStorage& storage_;
};

// Loads one scheduler per GatewayConfig.schedulers entry via
// CreateGatewayScheduler and composes them into a router. Fails on an empty
// list, an unknown scheduler name, or ambiguous bindings (duplicate task_type
// or more than one default).
auto BuildSchedulerRouter(const config::GatewayConfig& config,
                          extensions::GatewayFactoryContext& context)
    -> absl::StatusOr<std::unique_ptr<SchedulerRouter>>;

} // namespace strij::gateway