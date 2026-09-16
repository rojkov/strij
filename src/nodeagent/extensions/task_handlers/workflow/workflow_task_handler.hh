#pragma once

#include <string>

#include "absl/status/statusor.h"
#include "common/node/capabilities.pb.h"
#include "nodeagent/extensions/task_handlers/task_handlers.hh"
#include "strij/nodeagent/child_task_submitter.hh"

namespace strij::nodeagent::task_handlers {

/**
 * @brief Example handler that fans out to child tasks and aggregates their
 * final bodies into the parent result.
 *
 * Children are routed through the node's ChildTaskSubmitter (the local
 * scheduler router), so their authority follows the scheduler declarations
 * (a claiming task_type entry, or the local_default fallback). The handler is
 * built for the single event-loop thread: each child completes synchronously
 * within Submit(), so results are collected as the plan is walked. If any
 * child reports an error, aggregation aborts and the parent receives a final
 * result whose body describes the failure.
 */
class WorkflowTaskHandler final : public TaskHandler {
public:
  explicit WorkflowTaskHandler(strij::nodeagent::ChildTaskSubmitter& submitter);
  void HandleTask(const task::Task& task, ResultSenderPtr sender) override;

private:
  strij::nodeagent::ChildTaskSubmitter& submitter_;
};

class WorkflowTaskHandlerFactory final : public TaskHandlerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, extensions::NodeagentFactoryContext& context)
      -> TaskHandlerPtr override;
  auto ParseConfig(const ::google::protobuf::Message& config)
      -> absl::StatusOr<node::HandlerCapacity> override;
};

} // namespace strij::nodeagent::task_handlers