#pragma once

#include <span>
#include <string>
#include <string_view>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/nodeagent/run_task_service.hh"
#include "core/task/task.pb.h"
#include "extensions/schedulers/scheduler.hh"
#include "strij/event/command.hh"
#include "strij/event/command_handler.hh"

namespace strij::extensions::schedulers {

// The v1 scheduling protocol: gateways push kTaskSubmission frames straight to
// a node, and the node runs them to completion locally. This is the nodeagent
// half of the protocol; the gateway half is supplied by the round_robin and
// capability_aware gateway-side schedulers, which share the same
// RequiredProtocol() ("push").
//
// The scheduler owns the kTaskSubmission frame type in the NodeagentTlvHandler
// dispatch table, parses the task, and delegates execution to the shared
// nodeagent::RunTaskService. It also plays the event::CommandHandler role
// required by Connection's destination contract; ProcessCommand is a no-op.
class PushLocalScheduler final : public Scheduler, public event::CommandHandler {
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
  auto HandleFrame(io::TlvFrame frame, io::Connection& conn) -> absl::Status override;
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
  auto Create(const ::google::protobuf::Message& config, NodeagentFactoryContext& context)
      -> SchedulerPtr override;
};

} // namespace strij::extensions::schedulers