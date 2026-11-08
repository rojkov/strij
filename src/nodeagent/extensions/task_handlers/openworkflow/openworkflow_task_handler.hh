#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "common/config/extensions.pb.h"
#include "common/node/capabilities.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/interpreter.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/run_workflow.hh"
#include "strij/event/command_handler.hh"
#include "strij/event/dispatcher.hh"
#include "strij/nodeagent/child_task_submitter.hh"
#include "strij/nodeagent/event_bus.hh"
#include "strij/nodeagent/function_resolver.hh"
#include "strij/nodeagent/task_handlers.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

// The `openworkflow` task handler. It is a thin adapter over the interpreter:
// `HandleTask` resolves the function reference, loads the workflow artifact,
// hands the document and the task body (the invocation input) to a per-task
// interpreter, and retains the `ResultSender` for async delivery. Each
// interpreter is owned by a `shared_ptr` and the child receivers hold a
// `weak_ptr` back to it, so a late child outcome is dropped rather than routed
// to a freed interpreter.
class OpenWorkflowTaskHandler final : public TaskHandler, public event::CommandHandler {
public:
  OpenWorkflowTaskHandler(ChildTaskSubmitter& submitter, FunctionResolver& resolver,
                          config::ExtensionConfig evaluator, EventBus* event_bus = nullptr,
                          event::Dispatcher* dispatcher = nullptr);
  ~OpenWorkflowTaskHandler() override = default;

  OpenWorkflowTaskHandler(const OpenWorkflowTaskHandler&) = delete;
  auto operator=(const OpenWorkflowTaskHandler&) -> OpenWorkflowTaskHandler& = delete;
  OpenWorkflowTaskHandler(OpenWorkflowTaskHandler&&) noexcept = delete;
  auto operator=(OpenWorkflowTaskHandler&&) noexcept -> OpenWorkflowTaskHandler& = delete;

  void HandleTask(const task::Task& task, ResultSenderPtr sender) override;

  // event::CommandHandler interface
  void ProcessCommand(event::Command cmd) override;

private:
  struct Run {
    std::shared_ptr<WorkflowInterpreter> interpreter_;
    ResultSenderPtr sender_;
    std::optional<std::size_t> close_token_;
  };

  void send(const std::string& task_id, task::TaskResult result);
  void onClose(const std::string& task_id);
  void deferRunReap(std::unique_ptr<Run> run);

  ChildTaskSubmitter& submitter_;
  FunctionResolver& resolver_;
  config::ExtensionConfig evaluator_;
  EventBus* event_bus_{nullptr};
  // Optional for direct unit-test construction; the factory always supplies the
  // node dispatcher. With a dispatcher, completed runs are destroyed through a
  // DEFERRED_DELETE command so no completion callback can drop the last
  // interpreter reference while `drive()` is still on the stack.
  event::Dispatcher* dispatcher_{nullptr};
  // Owns nested interpreters started by `run.workflow` steps. It outlives every
  // interpreter, including detached runs that continue past a parent run.
  NestedWorkflowRunner runner_;
  std::map<std::string, std::unique_ptr<Run>> runs_;
  // Runs moved out of `runs_` but not yet destroyed. With a dispatcher the
  // matching DEFERRED_DELETE command erases the entry; without one (tests) the
  // entry lives until the handler is destroyed.
  std::vector<std::unique_ptr<Run>> reaped_;
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
