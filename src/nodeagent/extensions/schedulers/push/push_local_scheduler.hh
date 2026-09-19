#pragma once

#include <span>
#include <string>
#include <string_view>

#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/task/task.pb.h"
#include "strij/event/command.hh"
#include "strij/event/command_handler.hh"
#include "strij/extensions/scheduler.hh"
#include "strij/nodeagent/run_task_service.hh"

namespace strij::nodeagent::schedulers {

// The v1 scheduling protocol: gateways push kTaskSubmission frames straight to
// a node, and the node runs them to completion locally. This is the nodeagent
// half of the protocol; the gateway half is supplied by the round_robin and
// capability_aware gateway-side schedulers, which share the same
// RequiredProtocol() ("push").
//
// The scheduler owns the kTaskSubmission frame type in the node frame
// dispatcher, parses the task, and delegates execution to the shared
// nodeagent::RunTaskService. It is a pure wire-protocol counterpart: its node-
// local Schedule facet is unimplemented (it logs and delivers an error through
// the receiver, honoring the resolve contract). Locally-originated children
// run under the bundled "default" scheduler, the single local authority.
// It also plays the event::CommandHandler role required by Connection's
// destination contract; ProcessCommand is a no-op.
class PushLocalScheduler final : public extensions::Scheduler, public event::CommandHandler {
public:
  explicit PushLocalScheduler(nodeagent::RunTaskService& run_task_service)
      : run_task_service_{run_task_service} {}
  ~PushLocalScheduler() override = default;

  PushLocalScheduler(const PushLocalScheduler&) = delete;
  auto operator=(const PushLocalScheduler&) -> PushLocalScheduler& = delete;
  PushLocalScheduler(PushLocalScheduler&&) noexcept = delete;
  auto operator=(PushLocalScheduler&&) noexcept -> PushLocalScheduler& = delete;

  void Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) override;
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override;
  auto HandleFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status override;
  [[nodiscard]] auto HandledFrameTypes() const -> std::span<const uint8_t> override;

  void ProcessCommand(event::Command /*cmd*/) override {}

private:
  nodeagent::RunTaskService& run_task_service_;
};

class PushLocalSchedulerFactory final : public NodeSchedulerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override { return "push"; }
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override { return "push"; }
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config,
              extensions::NodeagentFactoryContext& context) -> extensions::SchedulerPtr override;
};

} // namespace strij::nodeagent::schedulers