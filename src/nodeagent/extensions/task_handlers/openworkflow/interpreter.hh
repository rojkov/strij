#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/config/extensions.pb.h"
#include "common/core/utils/jv.hh"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/errors.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/expression.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/step_data_value.hh"
#include "openworkflow/document.hh"
#include "openworkflow/task.hh"
#include "strij/nodeagent/child_task_submitter.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

// The terminal outcome of one child task, as observed by the interpreter.
struct ChildOutcome {
  bool is_error_ = false;
  std::string body_;
  std::string reason_;
  task::TaskStatus status_ = task::TASK_STATUS_INTERNAL;
};

// The handler side of the child-result seam. A child receiver calls back into
// this object (never into the interpreter directly), so a result arriving after
// the interpreter is gone is dropped rather than dereferencing freed state.
class ChildOutcomeSink {
public:
  ChildOutcomeSink() = default;
  virtual ~ChildOutcomeSink() = default;

  ChildOutcomeSink(const ChildOutcomeSink&) = delete;
  auto operator=(const ChildOutcomeSink&) -> ChildOutcomeSink& = delete;
  ChildOutcomeSink(ChildOutcomeSink&&) noexcept = delete;
  auto operator=(ChildOutcomeSink&&) noexcept -> ChildOutcomeSink& = delete;

  virtual void OnChildOutcome(const std::string& parent_id, const std::string& child_id,
                              ChildOutcome outcome) = 0;
  virtual void Reap() = 0;
};

// Flattens a document's `do` body (recursing through nested `do` containers)
// into the ordered steps the interpreter runs.
auto CollectSteps(const ::strij::openworkflow::Document& document)
    -> std::vector<const ::strij::openworkflow::NamedTask*>;

// The data-independent validation pass (design D7): call targets, the `with`
// key set, requirement coercion, and compiling every strict-mode expression.
// Returns the first failure, or nullopt when the document passes. Exposed so
// the split from execution-time failures can be exercised directly.
auto ValidateDocument(const ::strij::openworkflow::Document& document,
                      const std::vector<const ::strij::openworkflow::NamedTask*>& steps,
                      ExpressionEngine& engine) -> std::optional<Failure>;

// The per-task resumable state machine (design D2). It advances one step at a
// time; a step that submits a child suspends until that child's terminal
// outcome arrives, possibly before `Submit` even returns.
class WorkflowInterpreter {
public:
  using Completion = std::move_only_function<void(task::TaskResult)>;

  WorkflowInterpreter(ChildTaskSubmitter& submitter, config::ExtensionConfig evaluator,
                      ChildOutcomeSink& sink, std::string parent_id, Completion on_complete);
  ~WorkflowInterpreter() = default;

  WorkflowInterpreter(const WorkflowInterpreter&) = delete;
  auto operator=(const WorkflowInterpreter&) -> WorkflowInterpreter& = delete;
  WorkflowInterpreter(WorkflowInterpreter&&) noexcept = delete;
  auto operator=(WorkflowInterpreter&&) noexcept -> WorkflowInterpreter& = delete;

  // Parses, validates, and runs the document. The completion callback fires
  // exactly once (success or failure) unless the run is cancelled first.
  void Start(std::string document_text);

  // Delivers a terminal child outcome. Outcomes for an id other than the one
  // currently awaited are ignored.
  void OnChildOutcome(const std::string& child_id, ChildOutcome outcome);

  // Abandons the run: no result is delivered and outstanding children are not
  // awaited.
  void Cancel();

  [[nodiscard]] auto Finished() const -> bool { return finished_ || cancelled_; }

private:
  struct Pending {
    std::string child_id_;
    ChildOutcome outcome_;
  };

  auto validateMode() -> absl::Status;
  void run();
  void executeStep(const ::strij::openworkflow::NamedTask& step);
  auto applyOutcome(const std::string& child_id, ChildOutcome outcome) -> bool;
  auto buildChildBody(const ::strij::openworkflow::Task& task, const utils::Jv* subject)
      -> absl::StatusOr<std::string>;
  auto renderFrom(const std::variant<std::string, ::strij::openworkflow::Value>& from,
                  const utils::Jv* subject) -> absl::StatusOr<std::string>;
  void succeed();
  void fail(task::TaskStatus status, std::string_view step, std::string_view message);
  [[nodiscard]] auto currentStepName() const -> std::string;

  ChildTaskSubmitter& submitter_;
  ExpressionEngine engine_;
  ChildOutcomeSink& sink_;
  std::string parent_id_;
  Completion on_complete_;

  std::optional<::strij::openworkflow::Document> document_;
  std::vector<const ::strij::openworkflow::NamedTask*> steps_;
  StepDataValue data_;
  std::size_t step_index_ = 0;
  std::optional<std::string> waiting_child_id_;
  std::optional<Pending> pending_;
  bool in_submit_ = false;
  bool finished_ = false;
  bool cancelled_ = false;
};

} // namespace strij::nodeagent::task_handlers::openworkflow
