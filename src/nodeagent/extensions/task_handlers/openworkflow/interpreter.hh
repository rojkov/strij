#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/config/extensions.pb.h"
#include "common/core/utils/jv.hh"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/errors.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/expression.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/frame.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/step_data_value.hh"
#include "openworkflow/document.hh"
#include "openworkflow/task.hh"
#include "strij/nodeagent/child_task_submitter.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

class NestedWorkflowRunner;

// The data-independent validation pass: call targets, the `with` key set,
// requirement coercion, and compiling every strict-mode expression. It recurses
// every task in every nested sequence, including fork branch tasks, and rejects
// a competing fork with no branches. Returns the first failure, or nullopt when
// the document passes.
auto ValidateDocument(const ::strij::openworkflow::Document& document, ExpressionEngine& engine)
    -> std::optional<Failure>;

// The resumable workflow state machine. It advances the frame tree explicitly;
// a task that submits a child suspends its sequence until that child's terminal
// outcome arrives, possibly before `Submit` even returns. Fork branches advance
// concurrently, each on its own chain.
//
// Owned by a `shared_ptr` (the handler): the per-child receivers hold a
// `weak_ptr` back to it, so a child outcome that arrives after the run is gone
// is dropped rather than touching freed state.
class WorkflowInterpreter final : public std::enable_shared_from_this<WorkflowInterpreter>,
                                  private FrameContext {
public:
  using Completion = std::move_only_function<void(task::TaskResult)>;

  WorkflowInterpreter(ChildTaskSubmitter& submitter, config::ExtensionConfig evaluator,
                      std::string parent_id, Completion on_complete,
                      std::size_t for_iteration_budget = kDefaultSynchronousIterationBudget,
                      NestedWorkflowRunner* runner = nullptr, std::size_t depth = 0);
  ~WorkflowInterpreter() = default;

  WorkflowInterpreter(const WorkflowInterpreter&) = delete;
  auto operator=(const WorkflowInterpreter&) -> WorkflowInterpreter& = delete;
  WorkflowInterpreter(WorkflowInterpreter&&) noexcept = delete;
  auto operator=(WorkflowInterpreter&&) noexcept -> WorkflowInterpreter& = delete;

  // Parses, validates, and runs the document. The invocation input seeds the
  // data chain: an empty input means JSON null, and document-level `input.from`
  // is evaluated with that input as its subject. The completion callback fires
  // exactly once (success or failure) unless the run is cancelled first.
  void Start(std::string_view document_text, std::string_view invocation_input = {});

  // Delivers a terminal child outcome. Outcomes for an unknown or abandoned
  // child id are ignored.
  void OnChildOutcome(const std::string& child_id, ChildOutcome outcome);

  // Abandons the run: no result is delivered and outstanding children are not
  // awaited.
  void Cancel();

  [[nodiscard]] auto Finished() const -> bool { return IsTerminalState(state_); }

private:
  // The interpreter's lifecycle. `kAdvancing` is the reentrancy state: the
  // frame tree is being walked (including while `ChildTaskSubmitter::Submit`
  // is on the stack), so child outcomes are queued instead of applied
  // immediately. `kReady` means the drive loop is not on the stack, and a newly
  // arrived outcome can be applied and drive the workflow directly.
  enum class State : std::uint8_t {
    kIdle = 0,
    kReady = 1,
    kAdvancing = 2,
    kSucceeded = 3,
    kFailed = 4,
    kCancelled = 5,
  };
  [[nodiscard]] static constexpr auto IsTerminalState(State state) -> bool {
    return state == State::kSucceeded || state == State::kFailed || state == State::kCancelled;
  }

  // The only entry point for lifecycle changes; invalid transitions are
  // programmer errors.
  void setState(State new_state);

  auto validateMode() -> absl::Status;
  void drive();
  void terminalize(FrameResult result);

  auto EvaluateCondition(const ::strij::openworkflow::NamedTask& step, const StepDataValue& raw,
                         const LexicalScope* scope) -> absl::StatusOr<bool> override;
  auto TransformTaskInput(const ::strij::openworkflow::NamedTask& step, const StepDataValue& raw,
                          const LexicalScope* scope) -> absl::StatusOr<StepDataValue> override;
  auto TransformTaskOutput(const ::strij::openworkflow::NamedTask& step, const StepDataValue& input,
                           const StepDataValue& raw_output, const LexicalScope* scope)
      -> absl::StatusOr<StepDataValue> override;
  auto ExportTaskResult(const ::strij::openworkflow::NamedTask& step, const StepDataValue& input,
                        const StepDataValue& transformed_output, const LexicalScope* scope)
      -> absl::Status override;
  auto EvaluateSet(const ::strij::openworkflow::NamedTask& step, const StepDataValue& input,
                   const LexicalScope* scope) -> absl::StatusOr<StepDataValue> override;
  auto SelectSwitchDirective(const ::strij::openworkflow::NamedTask& step,
                             const StepDataValue& input, const LexicalScope* scope)
      -> absl::StatusOr<std::optional<std::string>> override;
  auto ExecuteCall(SequenceFrame& owner, const ::strij::openworkflow::NamedTask& step,
                   const StepDataValue& input, const LexicalScope* scope) -> absl::Status override;
  auto ExecuteRun(SequenceFrame& owner, const ::strij::openworkflow::NamedTask& step,
                  const StepDataValue& input, const LexicalScope* scope) -> absl::Status override;
  auto EvaluateForIn(const ::strij::openworkflow::NamedTask& step, const StepDataValue& input,
                     const LexicalScope* scope) -> absl::StatusOr<StepDataValue> override;
  auto EvaluateForWhile(const ::strij::openworkflow::NamedTask& step,
                        const StepDataValue& accumulator, const LexicalScope& scope)
      -> absl::StatusOr<bool> override;
  void ForgetChild(std::string_view child_id) override;

  auto transformInput(const std::variant<std::string, ::strij::openworkflow::Value>& from_value,
                      const StepDataValue& raw, const LexicalScope* scope)
      -> absl::StatusOr<StepDataValue>;
  auto transformOutput(const std::variant<std::string, ::strij::openworkflow::Value>& as_value,
                       const StepDataValue& input, const StepDataValue& raw_output,
                       const LexicalScope* scope) -> absl::StatusOr<StepDataValue>;
  void succeed(StepDataValue value);
  void fail(task::TaskStatus status, std::string_view step, std::string_view message);

  ChildTaskSubmitter& submitter_;
  ExpressionEngine engine_;
  NestedWorkflowRunner* runner_{nullptr};
  std::size_t depth_{0};
  // The workflow-scope `$context`, seeded as `{}` at start and replaced only by
  // a task's `export.as`.
  utils::Jv context_;
  std::string parent_id_;
  Completion on_complete_;
  std::size_t for_iteration_budget_{kDefaultSynchronousIterationBudget};

  std::optional<::strij::openworkflow::Document> document_;
  std::unique_ptr<SequenceFrame> root_;
  std::unordered_map<std::string, SequenceFrame*> owner_by_child_;
  // Outcomes that arrive while `kAdvancing` (including inside `Submit`). Keyed
  // by child id; the advancing branch drains its own entry before continuing.
  std::map<std::string, ChildOutcome> pending_;
  State state_{State::kIdle};
};

} // namespace strij::nodeagent::task_handlers::openworkflow
