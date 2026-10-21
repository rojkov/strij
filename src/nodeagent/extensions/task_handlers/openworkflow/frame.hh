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

struct Failed {
  ExecutionFailure failure_;
};

using FrameResult = std::variant<Suspended, Completed, Failed>;

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
                                 const StepDataValue& raw) -> absl::StatusOr<bool> PURE;

  // Evaluates a task's `input.from` against the raw task input. Absent
  // `input.from` is the identity.
  virtual auto TransformTaskInput(const ::strij::openworkflow::NamedTask& step,
                                  const StepDataValue& raw) -> absl::StatusOr<StepDataValue> PURE;

  // Evaluates a task's `output.as` against the raw output, with the transformed
  // input available as `$input`. Absent `output.as` is the identity.
  virtual auto TransformTaskOutput(const ::strij::openworkflow::NamedTask& step,
                                   const StepDataValue& input, const StepDataValue& raw_output)
      -> absl::StatusOr<StepDataValue> PURE;

  // Runs one `call: strij` step for `owner`, projecting its arguments from the
  // transformed input, building the child body, submitting it, and applying an
  // inline outcome. A returned error is the step's own (local) failure.
  virtual auto ExecuteCall(SequenceFrame& owner, const ::strij::openworkflow::NamedTask& step,
                           const StepDataValue& input) -> absl::Status PURE;

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
                         std::optional<Branch> branch = std::nullopt)
      : steps_{steps}, branch_{branch} {}

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

private:
  void materializeChild();
  [[nodiscard]] auto currentStepName() const -> std::string;
  [[nodiscard]] auto terminalResult() -> std::optional<FrameResult>;
  [[nodiscard]] auto advanceCall(const ::strij::openworkflow::NamedTask& step,
                                 FrameContext& context) -> std::optional<FrameResult>;
  [[nodiscard]] auto advanceContainer(FrameContext& context) -> std::optional<FrameResult>;
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
  std::optional<ExecutionFailure> failure_;
};

// Concurrent branch sequences plus the join policy. All branches are created
// and seeded on `Enter`; later steps are never prebuilt.
class ForkFrame final : public Frame {
public:
  ForkFrame(std::span<const ::strij::openworkflow::NamedTask> branch_declarations, bool compete)
      : branch_declarations_{branch_declarations}, compete_{compete} {}

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
  std::vector<std::unique_ptr<SequenceFrame>> branches_;
  // One slot per branch, in declaration order.
  std::vector<std::optional<StepDataValue>> results_;
  std::size_t completed_count_{0};
};

// Creates the root sequence over the document's top-level `do` body. The caller
// enters it with the seed value.
auto CreateRootFrame(const ::strij::openworkflow::Document& document)
    -> std::unique_ptr<SequenceFrame>;

} // namespace strij::nodeagent::task_handlers::openworkflow
