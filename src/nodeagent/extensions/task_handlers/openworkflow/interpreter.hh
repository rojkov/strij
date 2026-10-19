#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
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
  [[nodiscard]] auto IsError() const -> bool { return status_ != task::TASK_STATUS_OK; }

  std::string body_;
  std::string reason_;
  task::TaskStatus status_{task::TASK_STATUS_INTERNAL};
};

class SequenceFrame;

// A sequence frame owns an ordered list of tasks, a cursor, and its own
// data value; a fork frame owns its branch sequences and the join state.
// Frames are created once from the document (the shape is static) and
// only their cursors and data change while the workflow runs.
enum class FrameKind : std::uint8_t { kSequence = 0, kFork = 1 };

class Frame {
public:
  Frame(FrameKind kind, SequenceFrame* parent_sequence)
      : kind_{kind}, parent_sequence_{parent_sequence} {}
  virtual ~Frame() = default;

  Frame(const Frame&) = delete;
  auto operator=(const Frame&) -> Frame& = delete;
  Frame(Frame&&) noexcept = delete;
  auto operator=(Frame&&) noexcept -> Frame& = delete;

  [[nodiscard]] auto Kind() const -> FrameKind { return kind_; }
  // The sequence whose cursor is suspended on this frame; null for the root
  // and for fork branch sequences (which have an owning fork instead).
  [[nodiscard]] auto ParentSequence() const -> SequenceFrame* { return parent_sequence_; }

private:
  FrameKind kind_;
  SequenceFrame* parent_sequence_;
};

class ForkFrame;
class FrameBuilder;

class SequenceFrame final : public Frame {
private:
  // The lifecycle of one ordered sequence. A sequence starts pending; it becomes
  // ready when its enclosing frame enters it, waiting while a child outcome it
  // submitted is outstanding, and completed when its cursor passes the last
  // step. Abandoned marks a fork branch that will never run again (its enclosing
  // fork resolved or failed).
  enum class State : std::uint8_t {
    kPending = 0,
    kReady = 1,
    kWaiting = 2,
    kCompleted = 3,
    kAbandoned = 4,
  };

public:
  SequenceFrame(SequenceFrame* parent_sequence, ForkFrame* owner_fork, std::size_t branch_index,
                std::string branch_name);

  ~SequenceFrame() override = default;

  SequenceFrame(const SequenceFrame&) = delete;
  auto operator=(const SequenceFrame&) -> SequenceFrame& = delete;
  SequenceFrame(SequenceFrame&&) noexcept = delete;
  auto operator=(SequenceFrame&&) noexcept -> SequenceFrame& = delete;

  [[nodiscard]] auto IsPending() const -> bool { return state_ == State::kPending; }
  [[nodiscard]] auto IsWaiting() const -> bool { return state_ == State::kWaiting; }
  [[nodiscard]] auto IsCompleted() const -> bool { return state_ == State::kCompleted; }
  [[nodiscard]] auto IsFinished() const -> bool;
  [[nodiscard]] auto WaitingChildId() const -> const std::string&;

  [[nodiscard]] auto Steps() const -> const std::vector<const ::strij::openworkflow::NamedTask*>& {
    return steps_;
  }
  [[nodiscard]] auto Children() const -> const std::vector<std::unique_ptr<Frame>>& {
    return children_;
  }
  [[nodiscard]] auto Cursor() const -> std::size_t { return cursor_; }
  [[nodiscard]] auto OwnerFork() const -> ForkFrame* { return owner_fork_; }
  [[nodiscard]] auto BranchIndex() const -> std::size_t { return branch_index_; }
  [[nodiscard]] auto BranchName() const -> const std::string& { return branch_name_; }

private:
  friend class WorkflowInterpreter;
  friend class FrameBuilder;

  // The only entry point for state changes. `child_id` is required for
  // kWaiting and rejected for every other state; abandoning an already
  // finished sequence is a no-op (so terminal state is never downgraded).
  void setState(State new_state, std::optional<std::string> child_id = std::nullopt);

  std::vector<const ::strij::openworkflow::NamedTask*> steps_;
  // Parallel to `steps_`: a nested sequence for a `do`, a fork for a `fork`,
  // null for a leaf task.
  std::vector<std::unique_ptr<Frame>> children_;
  std::size_t cursor_{0};
  StepDataValue data_;
  // Set when this sequence is one branch of a fork.
  ForkFrame* owner_fork_{nullptr};
  std::size_t branch_index_{0};
  std::string branch_name_;
  State state_{State::kPending};
  // Set only while state_ is kWaiting.
  std::optional<std::string> waiting_child_id_;
};

class ForkFrame final : public Frame {
private:
  // The lifecycle of one fork. A fork starts pending, runs once entered, and
  // resolves when its join condition is satisfied. Abandoned marks a nested fork
  // whose enclosing branch was abandoned before this fork resolved.
  enum class State : std::uint8_t {
    kPending = 0,
    kRunning = 1,
    kResolved = 2,
    kAbandoned = 3,
  };

public:
  ForkFrame(SequenceFrame* parent_sequence, bool compete)
      : Frame(FrameKind::kFork, parent_sequence), compete_{compete} {}
  ~ForkFrame() override = default;

  ForkFrame(const ForkFrame&) = delete;
  auto operator=(const ForkFrame&) -> ForkFrame& = delete;
  ForkFrame(ForkFrame&&) noexcept = delete;
  auto operator=(ForkFrame&&) noexcept -> ForkFrame& = delete;

  [[nodiscard]] auto IsPending() const -> bool { return state_ == State::kPending; }
  [[nodiscard]] auto IsResolved() const -> bool { return state_ == State::kResolved; }

  [[nodiscard]] auto Branches() const -> const std::vector<std::unique_ptr<SequenceFrame>>& {
    return branches_;
  }
  [[nodiscard]] auto BranchResults() const -> const std::vector<std::optional<StepDataValue>>& {
    return branch_results_;
  }
  [[nodiscard]] auto IsCompeting() const -> bool { return compete_; }

private:
  friend class WorkflowInterpreter;
  friend class FrameBuilder;

  // The only entry point for state changes. Abandoning an already resolved
  // fork is a no-op, so a terminal state is never downgraded.
  void setState(State new_state) {
    switch (new_state) {
    case State::kRunning:
      assert(state_ == State::kPending);
      break;
    case State::kResolved:
      assert(state_ == State::kRunning);
      break;
    case State::kAbandoned:
      if (state_ == State::kResolved || state_ == State::kAbandoned) {
        return;
      }
      assert(state_ == State::kPending || state_ == State::kRunning);
      break;
    case State::kPending:
      assert(false);
      return;
    }
    state_ = new_state;
  }

  std::vector<std::unique_ptr<SequenceFrame>> branches_;
  // One slot per branch, in declaration order. A branch result is recorded when
  // that branch's sequence completes.
  std::vector<std::optional<StepDataValue>> branch_results_;
  bool compete_{false};
  std::size_t completed_count_{0};
  State state_{State::kPending};
};

// Builds the static execution tree from the document's top-level `do` body.
// Exposed so tests can assert the tree shape and its initial cursors.
auto BuildFrames(const ::strij::openworkflow::Document& document) -> std::unique_ptr<SequenceFrame>;

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
class WorkflowInterpreter : public std::enable_shared_from_this<WorkflowInterpreter> {
public:
  using Completion = std::move_only_function<void(task::TaskResult)>;

  WorkflowInterpreter(ChildTaskSubmitter& submitter, config::ExtensionConfig evaluator,
                      std::string parent_id, Completion on_complete);
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
  void advanceSequence(SequenceFrame& sequence);
  auto advanceStep(SequenceFrame& sequence, const ::strij::openworkflow::NamedTask& step) -> bool;
  void advanceFork(ForkFrame& fork);
  void executeCall(SequenceFrame& sequence, const ::strij::openworkflow::NamedTask& step);
  void completeSequence(SequenceFrame& sequence);
  void onBranchComplete(ForkFrame& fork, SequenceFrame& branch);
  void resolveFork(ForkFrame& fork, StepDataValue value);
  void abandonFrame(Frame& frame);
  auto applyOutcome(SequenceFrame& sequence, const std::string& child_id, ChildOutcome outcome)
      -> bool;
  auto buildChildBody(const ::strij::openworkflow::Task& task, const StepDataValue& data,
                      const utils::Jv* subject) -> absl::StatusOr<std::string>;
  auto renderFrom(const std::variant<std::string, ::strij::openworkflow::Value>& from,
                  const utils::Jv* subject) -> absl::StatusOr<std::string>;
  void succeed();
  void fail(task::TaskStatus status, std::string_view step, std::string_view message);
  void failSequence(SequenceFrame& sequence, task::TaskStatus status, std::string_view message);

  ChildTaskSubmitter& submitter_;
  ExpressionEngine engine_;
  std::string parent_id_;
  Completion on_complete_;

  std::optional<::strij::openworkflow::Document> document_;
  std::unique_ptr<SequenceFrame> root_;
  std::unordered_map<std::string, SequenceFrame*> owner_by_child_;
  // Outcomes that arrive while `kAdvancing` (including inside `Submit`). Keyed
  // by child id; the advancing branch drains its own entry before continuing.
  std::map<std::string, ChildOutcome> pending_;
  State state_{State::kIdle};
};

} // namespace strij::nodeagent::task_handlers::openworkflow
