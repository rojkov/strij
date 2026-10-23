#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/expression.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/step_data_value.hh"
#include "openworkflow/document.hh"
#include "openworkflow/task.hh"
#include "strij/common/pure.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

// The terminal outcome of one child task, as observed by the interpreter.
struct ChildOutcome {
  [[nodiscard]] auto IsError() const -> bool { return status_ != task::TASK_STATUS_OK; }

  std::string body_;
  std::string reason_;
  task::TaskStatus status_{task::TASK_STATUS_INTERNAL};
};

class SequenceFrame;

// One immutable node in the lexical loop-variable chain. A sequence carries the
// innermost scope that is visible to its expressions; a `for` body installs a
// node whose parent is the declaring sequence's scope. Names are bare jq
// variable names (without `$`).
struct LexicalScope {
  std::shared_ptr<const LexicalScope> parent_;
  std::string each_name_;
  utils::Jv item_;
  std::string at_name_;
  utils::Jv index_;
};

// A failure travelling upward through the frame tree. `origin_` is the sequence
// whose cursor was on the failing step; it points at a frame that stays alive
// for the duration of the run. `step_` is that step's name. `composed_` marks a
// failure that has already been named by its nearest enclosing fork.
struct ExecutionFailure {
  task::TaskStatus status_{task::TASK_STATUS_INTERNAL};
  const SequenceFrame* origin_{nullptr};
  std::string step_;
  std::string message_;
  bool composed_{false};
};

struct Suspended {};

struct Completed {
  StepDataValue value_;
};

// The workflow must stop from any depth. Unlike `Completed`, an enclosing
// `do`/`fork` does not apply its own `output.as` or `export.as`.
struct Ended {
  StepDataValue value_;
};

struct Failed {
  ExecutionFailure failure_;
};

using FrameResult = std::variant<Suspended, Completed, Ended, Failed>;

// What a task's `then` directive does next. `kJump` names an index into the
// task's own sequence; the other kinds carry no target.
enum class FlowKind : std::uint8_t {
  kContinue = 0,
  kJump = 1,
  kExit = 2,
  kEnd = 3,
};

struct FlowDirective {
  FlowKind kind_{FlowKind::kContinue};
  std::size_t target_{0};
};

// Resolves a `then` value against the tasks of one sequence. `continue` (and an
// empty value) advances to the next task, `exit` completes the sequence, `end`
// concludes the workflow, and any other value must name exactly one task in
// `scope`. Shared by validation and execution so a validated document cannot
// fail runtime resolution.
auto ResolveFlowDirective(std::span<const ::strij::openworkflow::NamedTask> scope,
                          std::string_view then) -> absl::StatusOr<FlowDirective>;

// The number of steps one sequence may advance in a single synchronous stretch,
// and the number of iterations one `for` may advance in a single stretch.
// Generous so ordinary workflows are unaffected; a backward jump or a large
// synchronous loop trips it instead of wedging the event loop.
inline constexpr std::size_t kDefaultSynchronousTransitionBudget = 100000;
inline constexpr std::size_t kDefaultSynchronousIterationBudget =
    kDefaultSynchronousTransitionBudget;

// The effects a frame needs from its owner. The interpreter is the only
// implementation; frame tests supply a fake.
class FrameContext {
public:
  virtual ~FrameContext() = default;

  FrameContext(const FrameContext&) = delete;
  auto operator=(const FrameContext&) -> FrameContext& = delete;
  FrameContext(FrameContext&&) noexcept = delete;
  auto operator=(FrameContext&&) noexcept -> FrameContext& = delete;

  // Evaluates a task's `if` against the raw task input. Absent `if` is true.
  virtual auto EvaluateCondition(const ::strij::openworkflow::NamedTask& step,
                                 const StepDataValue& raw, const LexicalScope* scope)
      -> absl::StatusOr<bool> PURE;

  // Evaluates a task's `input.from` against the raw task input. Absent
  // `input.from` is the identity.
  virtual auto TransformTaskInput(const ::strij::openworkflow::NamedTask& step,
                                  const StepDataValue& raw, const LexicalScope* scope)
      -> absl::StatusOr<StepDataValue> PURE;

  // Evaluates a task's `output.as` against the raw output, with the transformed
  // input available as `$input`. Absent `output.as` is the identity.
  virtual auto TransformTaskOutput(const ::strij::openworkflow::NamedTask& step,
                                   const StepDataValue& input, const StepDataValue& raw_output,
                                   const LexicalScope* scope) -> absl::StatusOr<StepDataValue> PURE;

  // Applies a task's `export.as` against the shaped task output. Absent
  // `export`/`as` leaves the context unchanged. A returned error is the step's
  // own (local) failure.
  virtual auto ExportTaskResult(const ::strij::openworkflow::NamedTask& step,
                                const StepDataValue& input, const StepDataValue& transformed_output,
                                const LexicalScope* scope) -> absl::Status PURE;

  // Evaluates a `set` task's data against the transformed task input, using the
  // same subject rules as `input.from`.
  virtual auto EvaluateSet(const ::strij::openworkflow::NamedTask& step, const StepDataValue& input,
                           const LexicalScope* scope) -> absl::StatusOr<StepDataValue> PURE;

  // Selects the `then` of a `switch` case against the transformed task input.
  // Returns nullopt when no case matches, letting the sequence fall back to the
  // switch task's own `then`.
  virtual auto SelectSwitchDirective(const ::strij::openworkflow::NamedTask& step,
                                     const StepDataValue& input, const LexicalScope* scope)
      -> absl::StatusOr<std::optional<std::string>> PURE;

  // Runs one `call: strij` step for `owner`, projecting its arguments from the
  // transformed input, building the child body, submitting it, and applying an
  // inline outcome. A returned error is the step's own (local) failure.
  virtual auto ExecuteCall(SequenceFrame& owner, const ::strij::openworkflow::NamedTask& step,
                           const StepDataValue& input, const LexicalScope* scope)
      -> absl::Status PURE;

  // Runs one `run.workflow` step for `owner`. When the step awaits, it suspends
  // the owner under a synthetic child id and starts the nested run; an inline
  // outcome is applied by the context. When it does not await, the nested run is
  // started without suspending. A returned error is the step's own failure.
  virtual auto ExecuteRun(SequenceFrame& owner, const ::strij::openworkflow::NamedTask& step,
                          const StepDataValue& input, const LexicalScope* scope)
      -> absl::Status PURE;

  // Evaluates a `for` task's collection against its transformed input. The
  // result is a JSON array or null. `scope` is the declaring sequence's scope
  // (the loop's own variables are not visible to `in`).
  virtual auto EvaluateForIn(const ::strij::openworkflow::NamedTask& step,
                             const StepDataValue& input, const LexicalScope* scope)
      -> absl::StatusOr<StepDataValue> PURE;

  // Evaluates a `for` task's `while` after a completed iteration against the
  // accumulator. The scope includes that iteration's loop variables.
  virtual auto EvaluateForWhile(const ::strij::openworkflow::NamedTask& step,
                                const StepDataValue& accumulator, const LexicalScope& scope)
      -> absl::StatusOr<bool> PURE;

  // Drops a child id from the outcome-routing map. Called only when a waiting
  // frame is abandoned.
  virtual void ForgetChild(std::string_view child_id) PURE;

protected:
  FrameContext() = default;
};

// The shared lifecycle of a frame.
//
//   Pending --Enter--> Active --> Completed
//                        |
//                 error  +-- Abandon
//                        v    v
//                     Failed  Abandoned
//
// Completed, Failed, and Abandoned are terminal; abandoning a terminal frame is
// a no-op. "Waiting on a child" is a sequence field, not a state.
class Frame {
public:
  enum class State : std::uint8_t {
    kPending = 0,
    kActive = 1,
    kCompleted = 2,
    kFailed = 3,
    kAbandoned = 4,
  };

  virtual ~Frame() = default;

  Frame(const Frame&) = delete;
  auto operator=(const Frame&) -> Frame& = delete;
  Frame(Frame&&) noexcept = delete;
  auto operator=(Frame&&) noexcept -> Frame& = delete;

  [[nodiscard]] auto Lifecycle() const -> Frame::State { return state_; }
  [[nodiscard]] auto IsPending() const -> bool { return state_ == State::kPending; }
  [[nodiscard]] auto IsActive() const -> bool { return state_ == State::kActive; }
  [[nodiscard]] auto IsCompleted() const -> bool { return state_ == State::kCompleted; }
  [[nodiscard]] auto IsFailed() const -> bool { return state_ == State::kFailed; }
  [[nodiscard]] auto IsAbandoned() const -> bool { return state_ == State::kAbandoned; }
  [[nodiscard]] auto IsTerminal() const -> bool {
    return state_ == State::kCompleted || state_ == State::kFailed || state_ == State::kAbandoned;
  }

  // Seeds the frame's data chain and marks it active.
  virtual void Enter(StepDataValue data) PURE;

  // Advances the frame as far as it can go without an outstanding child.
  virtual auto Advance(FrameContext& context) -> FrameResult PURE;

  // Marks the frame abandoned and recursively forgets its waiting child ids. It
  // never destroys or resets children, so it is safe to call while a fork
  // iterates its branches.
  virtual void Abandon(FrameContext& context) PURE;

protected:
  Frame() = default;

  void markActive();
  void markCompleted();
  void markFailed();
  // No-op when the frame is already terminal.
  void markAbandoned();

private:
  State state_{State::kPending};
};

// An ordered list of steps plus the single value flowing through them. The step
// span and the branch name are views into the document, which the interpreter
// owns for the whole run. A nested container is materialized only when its step
// is entered, and at most one is live at a time.
class SequenceFrame final : public Frame {
public:
  struct Branch {
    std::size_t index_{0};
    std::string_view name_;
  };

  explicit SequenceFrame(std::span<const ::strij::openworkflow::NamedTask> steps,
                         std::optional<Branch> branch = std::nullopt,
                         std::size_t transition_budget = kDefaultSynchronousTransitionBudget,
                         std::shared_ptr<const LexicalScope> scope = nullptr,
                         std::size_t for_iteration_budget = kDefaultSynchronousIterationBudget)
      : steps_{steps}, branch_{branch}, scope_{std::move(scope)},
        transition_budget_{transition_budget}, for_iteration_budget_{for_iteration_budget} {}

  ~SequenceFrame() override = default;

  SequenceFrame(const SequenceFrame&) = delete;
  auto operator=(const SequenceFrame&) -> SequenceFrame& = delete;
  SequenceFrame(SequenceFrame&&) noexcept = delete;
  auto operator=(SequenceFrame&&) noexcept -> SequenceFrame& = delete;

  void Enter(StepDataValue data) override;
  auto Advance(FrameContext& context) -> FrameResult override;
  void Abandon(FrameContext& context) override;

  // Records the child this sequence is waiting on. Precondition: active and not
  // already waiting.
  void Suspend(std::string child_id);

  // Clears the current wait without delivering an outcome. Used when a child
  // fails to start after the wait was registered.
  void ClearWait();

  // Applies a terminal child outcome: clears the wait, then either advances the
  // data chain and cursor (applying `output.as`), or stores the failure and
  // marks this frame failed.
  void ApplyOutcome(ChildOutcome outcome, FrameContext& context);

  [[nodiscard]] auto Steps() const -> std::span<const ::strij::openworkflow::NamedTask> {
    return steps_;
  }
  [[nodiscard]] auto Cursor() const -> std::size_t { return cursor_; }
  [[nodiscard]] auto Data() const -> const StepDataValue& { return data_; }
  [[nodiscard]] auto IsWaiting() const -> bool { return waiting_child_id_.has_value(); }
  [[nodiscard]] auto WaitingChildId() const -> const std::string& { return *waiting_child_id_; }
  [[nodiscard]] auto Child() const -> const Frame* { return child_.get(); }
  [[nodiscard]] auto BranchInfo() const -> const std::optional<Branch>& { return branch_; }
  [[nodiscard]] auto Scope() const -> const std::shared_ptr<const LexicalScope>& { return scope_; }

private:
  void materializeChild();
  [[nodiscard]] auto currentStepName() const -> std::string;
  [[nodiscard]] auto terminalResult() -> std::optional<FrameResult>;
  [[nodiscard]] auto advanceCall(const ::strij::openworkflow::NamedTask& step,
                                 FrameContext& context) -> std::optional<FrameResult>;
  [[nodiscard]] auto advanceRun(const ::strij::openworkflow::NamedTask& step,
                                FrameContext& context) -> std::optional<FrameResult>;
  [[nodiscard]] auto advanceContainer(FrameContext& context) -> std::optional<FrameResult>;
  // Applies `output.as` and `export.as` to a completed step's raw output, then
  // the step's flow directive. A returned value is a failure; an `exit`/`end`
  // directive is stored in `pending_result_` for the next `Advance` to return.
  [[nodiscard]] auto applyCompletedStep(const ::strij::openworkflow::NamedTask& step,
                                        StepDataValue raw_output, FrameContext& context,
                                        std::string_view then) -> std::optional<FrameResult>;
  [[nodiscard]] auto applyFlowDirective(const ::strij::openworkflow::NamedTask& step,
                                        std::string_view then) -> std::optional<FrameResult>;
  [[nodiscard]] auto failStep(task::TaskStatus status, const ::strij::openworkflow::NamedTask& step,
                              std::string message) -> FrameResult;
  [[nodiscard]] auto storedFailureResult() -> FrameResult;

  std::span<const ::strij::openworkflow::NamedTask> steps_;
  std::size_t cursor_{0};
  StepDataValue data_;
  // The current step's transformed input, set at step entry and cleared when
  // the step completes. It remains set while the step waits on a child so a
  // resumed outcome can apply `output.as` with the right `$input`.
  std::optional<StepDataValue> step_input_;
  std::unique_ptr<Frame> child_;
  std::optional<std::string> waiting_child_id_;
  std::optional<Branch> branch_;
  std::shared_ptr<const LexicalScope> scope_;
  std::optional<ExecutionFailure> failure_;
  // Set by an `exit`/`end` directive while a completion sink is on the stack;
  // consumed by the next `Advance`.
  std::optional<FrameResult> pending_result_;
  std::size_t transitions_{0};
  std::size_t transition_budget_{kDefaultSynchronousTransitionBudget};
  std::size_t for_iteration_budget_{kDefaultSynchronousIterationBudget};
};

// A sequential `for` composite. It evaluates `for.in` once, then lazily creates
// one body sequence per element. The accumulator starts as the transformed input
// and is replaced by each completed iteration's value, so zero iterations make
// the loop an identity. A failed body is kept alive because its
// `ExecutionFailure::origin_` may point into it.
class ForFrame final : public Frame {
public:
  ForFrame(const ::strij::openworkflow::NamedTask& task, const ::strij::openworkflow::ForBody& body,
           std::shared_ptr<const LexicalScope> scope,
           std::size_t iteration_budget = kDefaultSynchronousIterationBudget);

  ~ForFrame() override = default;

  ForFrame(const ForFrame&) = delete;
  auto operator=(const ForFrame&) -> ForFrame& = delete;
  ForFrame(ForFrame&&) noexcept = delete;
  auto operator=(ForFrame&&) noexcept -> ForFrame& = delete;

  void Enter(StepDataValue data) override;
  auto Advance(FrameContext& context) -> FrameResult override;
  void Abandon(FrameContext& context) override;

  [[nodiscard]] auto Accumulator() const -> const StepDataValue& { return accumulator_; }
  [[nodiscard]] auto NextIndex() const -> std::size_t { return next_index_; }
  [[nodiscard]] auto BodyFrame() const -> const SequenceFrame* { return body_frame_.get(); }
  [[nodiscard]] auto IterationScope() const -> const LexicalScope* {
    return iteration_scope_.get();
  }

private:
  [[nodiscard]] auto startCollection(FrameContext& context) -> std::optional<FrameResult>;
  [[nodiscard]] auto fail(std::string message) -> FrameResult;
  void beginIteration();

  const ::strij::openworkflow::NamedTask& task_;
  const ::strij::openworkflow::ForBody& body_;
  std::shared_ptr<const LexicalScope> scope_;
  LoopVariableNames names_;
  bool started_{false};
  ::strij::openworkflow::Value::Array items_;
  std::size_t next_index_{0};
  StepDataValue accumulator_;
  std::unique_ptr<SequenceFrame> body_frame_;
  std::shared_ptr<const LexicalScope> iteration_scope_;
  std::optional<ExecutionFailure> failure_;
  std::size_t iterations_{0};
  std::size_t iteration_budget_{kDefaultSynchronousIterationBudget};
};

// Concurrent branch sequences plus the join policy. All branches are created
// and seeded on `Enter`; later steps are never prebuilt.
class ForkFrame final : public Frame {
public:
  ForkFrame(std::span<const ::strij::openworkflow::NamedTask> branch_declarations, bool compete,
            std::shared_ptr<const LexicalScope> scope = nullptr,
            std::size_t for_iteration_budget = kDefaultSynchronousIterationBudget)
      : branch_declarations_{branch_declarations}, compete_{compete}, scope_{std::move(scope)},
        for_iteration_budget_{for_iteration_budget} {}

  ~ForkFrame() override = default;

  ForkFrame(const ForkFrame&) = delete;
  auto operator=(const ForkFrame&) -> ForkFrame& = delete;
  ForkFrame(ForkFrame&&) noexcept = delete;
  auto operator=(ForkFrame&&) noexcept -> ForkFrame& = delete;

  void Enter(StepDataValue data) override;
  auto Advance(FrameContext& context) -> FrameResult override;
  void Abandon(FrameContext& context) override;

  [[nodiscard]] auto Branches() const -> const std::vector<std::unique_ptr<SequenceFrame>>& {
    return branches_;
  }
  [[nodiscard]] auto BranchResults() const -> const std::vector<std::optional<StepDataValue>>& {
    return results_;
  }
  [[nodiscard]] auto IsCompeting() const -> bool { return compete_; }

private:
  void abandonBranches(FrameContext& context);
  [[nodiscard]] static auto composeFailure(const SequenceFrame& branch, ExecutionFailure failure)
      -> ExecutionFailure;

  // The fork's declared branches, owned by the document.
  std::span<const ::strij::openworkflow::NamedTask> branch_declarations_;
  bool compete_{false};
  std::shared_ptr<const LexicalScope> scope_;
  std::size_t for_iteration_budget_{kDefaultSynchronousIterationBudget};
  std::vector<std::unique_ptr<SequenceFrame>> branches_;
  // One slot per branch, in declaration order.
  std::vector<std::optional<StepDataValue>> results_;
  std::size_t completed_count_{0};
};

// Creates the root sequence over the document's top-level `do` body. The caller
// enters it with the seed value.
auto CreateRootFrame(const ::strij::openworkflow::Document& document,
                     std::size_t for_iteration_budget = kDefaultSynchronousIterationBudget)
    -> std::unique_ptr<SequenceFrame>;

} // namespace strij::nodeagent::task_handlers::openworkflow
