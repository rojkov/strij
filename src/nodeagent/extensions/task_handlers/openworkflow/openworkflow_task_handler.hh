#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "common/config/extensions.pb.h"
#include "common/node/capabilities.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/interpreter.hh"
#include "strij/nodeagent/child_task_submitter.hh"
#include "strij/nodeagent/task_handlers.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

// The `openworkflow` task handler. It is a thin adapter over the interpreter:
// `HandleTask` parses the task body, hands it to a per-task interpreter, and
// retains the `ResultSender` for async delivery. It also owns the child-result
// seam so a child completing (synchronously or later) is routed to the right
// interpreter without the interpreter ever holding a freed pointer.
class OpenWorkflowTaskHandler final : public TaskHandler, public ChildOutcomeSink {
public:
  OpenWorkflowTaskHandler(ChildTaskSubmitter& submitter, config::ExtensionConfig evaluator);
  ~OpenWorkflowTaskHandler() override = default;

  OpenWorkflowTaskHandler(const OpenWorkflowTaskHandler&) = delete;
  auto operator=(const OpenWorkflowTaskHandler&) -> OpenWorkflowTaskHandler& = delete;
  OpenWorkflowTaskHandler(OpenWorkflowTaskHandler&&) noexcept = delete;
  auto operator=(OpenWorkflowTaskHandler&&) noexcept -> OpenWorkflowTaskHandler& = delete;

  void HandleTask(const task::Task& task, ResultSenderPtr sender) override;

  void OnChildOutcome(const std::string& parent_id, const std::string& child_id,
                      ChildOutcome outcome) override;

private:
  // Erases runs whose interpreter has completed or been cancelled. Called
  // after an entry point returns, so a run is never destroyed while an
  // interpreter method is still on the stack.
  void reap();

  struct Run {
    std::unique_ptr<WorkflowInterpreter> interpreter_;
    ResultSenderPtr sender_;
    std::optional<std::size_t> close_token_;
    bool finished_ = false;
  };

  void send(const std::string& task_id, task::TaskResult result);
  void onClose(const std::string& task_id);

  ChildTaskSubmitter& submitter_;
  config::ExtensionConfig evaluator_;
  std::map<std::string, Run> runs_;
};

class OpenWorkflowTaskHandlerFactory final : public TaskHandlerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, const TaskHandlerDeps& deps)
      -> TaskHandlerPtr override;
  auto ParseConfig(const ::google::protobuf::Message& config)
      -> absl::StatusOr<node::HandlerCapacity> override;
};

} // namespace strij::nodeagent::task_handlers::openworkflow
