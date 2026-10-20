#include "nodeagent/extensions/task_handlers/openworkflow/frame.hh"

#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/step_data_value.hh"
#include "openworkflow/document.hh"
#include "openworkflow/task.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

using ::strij::openworkflow::CallBody;
using ::strij::openworkflow::DoBody;
using ::strij::openworkflow::ForkBody;
using ::strij::openworkflow::NamedTask;
using ::strij::openworkflow::Task;

namespace {

auto joinAllForkResult(const std::vector<std::unique_ptr<SequenceFrame>>& branches,
                       const std::vector<std::optional<StepDataValue>>& results)
    -> std::variant<StepDataValue, ExecutionFailure> {
  std::string array = "[";
  for (std::size_t index = 0; index < branches.size(); ++index) {
    const StepDataValue& result = *results[index];
    if (!result.IsJson()) {
      return ExecutionFailure{
          .status_ = task::TASK_STATUS_INTERNAL,
          .origin_ = nullptr,
          .step_ = {},
          .message_ = absl::StrCat("branch '", branches[index]->BranchInfo()->name_,
                                   "': the branch result is not a JSON value"),
          .composed_ = true,
      };
    }

    if (index > 0) {
      array.push_back(',');
    }
    array.append(result.Bytes());
  }
  array.push_back(']');

  return StepDataValue::FromBytes(std::move(array));
}

} // namespace

void Frame::markActive() {
  assert(IsPending());
  state_ = State::kActive;
}

void Frame::markCompleted() {
  assert(IsActive());
  state_ = State::kCompleted;
}

void Frame::markFailed() {
  assert(IsActive());
  state_ = State::kFailed;
}

void Frame::markAbandoned() {
  if (IsTerminal()) {
    return;
  }

  assert(IsPending() || IsActive());
  state_ = State::kAbandoned;
}

auto CreateRootFrame(const ::strij::openworkflow::Document& document)
    -> std::unique_ptr<SequenceFrame> {
  return std::make_unique<SequenceFrame>(std::span<const NamedTask>(document.do_));
}

void SequenceFrame::Enter(StepDataValue data) {
  assert(IsPending());
  data_ = std::move(data);
  markActive();
}

auto SequenceFrame::currentStepName() const -> std::string {
  if (cursor_ < steps_.size()) {
    return steps_[cursor_].name_;
  }

  return {};
}

void SequenceFrame::materializeChild() {
  const Task& task = steps_[cursor_].task_;

  if (const auto* do_body = std::get_if<DoBody>(&task.body_)) {
    child_ = std::make_unique<SequenceFrame>(std::span<const NamedTask>(do_body->do_));
  } else if (const auto* fork_body = std::get_if<ForkBody>(&task.body_)) {
    child_ = std::make_unique<ForkFrame>(std::span<const NamedTask>(fork_body->branches_),
                                         fork_body->compete_);
  } else {
    assert(false);
    return;
  }

  child_->Enter(std::move(data_));
}

void SequenceFrame::Suspend(std::string child_id) {
  assert(IsActive());
  assert(!waiting_child_id_.has_value());
  waiting_child_id_ = std::move(child_id);
}

void SequenceFrame::ApplyOutcome(ChildOutcome outcome) {
  assert(IsActive());
  assert(waiting_child_id_.has_value());
  waiting_child_id_.reset();

  if (outcome.IsError()) {
    failure_ = ExecutionFailure{
        .status_ = outcome.status_,
        .origin_ = this,
        .step_ = currentStepName(),
        .message_ = std::move(outcome.reason_),
        .composed_ = false,
    };
    markFailed();

    return;
  }

  data_ = StepDataValue::FromBytes(std::move(outcome.body_));
  ++cursor_;
}

auto SequenceFrame::terminalResult() -> std::optional<FrameResult> {
  if (IsFailed()) {
    assert(failure_.has_value());
    return Failed{.failure_ = *failure_};
  }
  if (IsAbandoned()) {
    return Suspended{};
  }

  return std::nullopt;
}

auto SequenceFrame::advanceCall(const NamedTask& step, FrameContext& context)
    -> std::optional<FrameResult> {
  const absl::Status status = context.ExecuteCall(*this, step);
  if (!status.ok()) {
    return failStep(task::TASK_STATUS_INTERNAL, step, std::string(status.message()));
  }

  // A local child may complete before `ExecuteCall` returns. Its outcome is
  // applied by the context; resume from whatever state that produced.
  if (failure_.has_value()) {
    return storedFailureResult();
  }
  if (waiting_child_id_.has_value()) {
    return Suspended{};
  }

  return std::nullopt;
}

auto SequenceFrame::advanceContainer(FrameContext& context) -> std::optional<FrameResult> {
  if (child_ == nullptr) {
    materializeChild();
  }

  FrameResult result = child_->Advance(context);
  if (auto* failed = std::get_if<Failed>(&result); failed != nullptr) {
    // Keep the failed child alive: `failure.origin_` may point into it, and an
    // enclosing fork still needs that pointer to decide whether to name the
    // failing step.
    failure_ = std::move(failed->failure_);
    markFailed();
    return Failed{.failure_ = *failure_};
  }
  if (std::holds_alternative<Suspended>(result)) {
    return Suspended{};
  }

  data_ = std::move(std::get<Completed>(result).value_);
  ++cursor_;
  child_.reset();

  return std::nullopt;
}

auto SequenceFrame::failStep(task::TaskStatus status, const NamedTask& step, std::string message)
    -> FrameResult {
  failure_ = ExecutionFailure{
      .status_ = status,
      .origin_ = this,
      .step_ = step.name_,
      .message_ = std::move(message),
      .composed_ = false,
  };
  markFailed();

  return Failed{.failure_ = *failure_};
}

auto SequenceFrame::storedFailureResult() -> FrameResult {
  assert(failure_.has_value());
  if (IsActive()) {
    markFailed();
  }

  return Failed{.failure_ = *failure_};
}

auto SequenceFrame::Advance(FrameContext& context) -> FrameResult {
  if (auto terminal = terminalResult(); terminal.has_value()) {
    return std::move(*terminal);
  }

  assert(IsActive());

  while (cursor_ < steps_.size()) {
    if (waiting_child_id_.has_value()) {
      return Suspended{};
    }
    if (failure_.has_value()) {
      return storedFailureResult();
    }

    const NamedTask& step = steps_[cursor_];
    const Task& task = step.task_;

    std::optional<FrameResult> step_result;
    if (std::holds_alternative<CallBody>(task.body_)) {
      step_result = advanceCall(step, context);
    } else if (std::holds_alternative<DoBody>(task.body_) ||
               std::holds_alternative<ForkBody>(task.body_)) {
      step_result = advanceContainer(context);
    } else {
      step_result = failStep(task::TASK_STATUS_UNIMPLEMENTED, step,
                             "this task kind is not supported in the current workflow slice");
    }

    if (step_result.has_value()) {
      return std::move(*step_result);
    }
  }

  markCompleted();
  return Completed{.value_ = std::move(data_)};
}

void SequenceFrame::Abandon(FrameContext& context) {
  if (IsTerminal()) {
    return;
  }

  if (waiting_child_id_.has_value()) {
    context.ForgetChild(*waiting_child_id_);
    waiting_child_id_.reset();
  }

  markAbandoned();

  if (child_ != nullptr) {
    child_->Abandon(context);
  }
}

void ForkFrame::Enter(StepDataValue data) {
  assert(IsPending());
  markActive();
  results_.resize(branch_declarations_.size());
  branches_.reserve(branch_declarations_.size());

  for (std::size_t index = 0; index < branch_declarations_.size(); ++index) {
    auto branch = std::make_unique<SequenceFrame>(
        std::span<const NamedTask>(&branch_declarations_[index], 1),
        SequenceFrame::Branch{.index_ = index, .name_ = branch_declarations_[index].name_});
    branch->Enter(data.Copy());
    branches_.push_back(std::move(branch));
  }
}

auto ForkFrame::composeFailure(const SequenceFrame& branch, ExecutionFailure failure)
    -> ExecutionFailure {
  std::string message = absl::StrCat("branch '", branch.BranchInfo()->name_, "': ");
  if (failure.origin_ != &branch && !failure.step_.empty()) {
    absl::StrAppend(&message, "step '", failure.step_, "': ");
  }
  absl::StrAppend(&message, failure.message_);

  failure.message_ = std::move(message);
  failure.step_.clear();
  failure.composed_ = true;

  return failure;
}

void ForkFrame::abandonBranches(FrameContext& context) {
  for (auto& branch : branches_) {
    branch->Abandon(context);
  }
}

auto ForkFrame::Advance(FrameContext& context) -> FrameResult {
  if (IsAbandoned()) {
    return Suspended{};
  }
  assert(IsActive());

  // An empty join-all fork is well defined: it joins to the empty array. An
  // empty competing fork is rejected during validation.
  if (branches_.empty()) {
    markCompleted();
    return Completed{.value_ = StepDataValue::FromBytes("[]")};
  }

  for (std::size_t index = 0; index < branches_.size(); ++index) {
    if (results_[index].has_value()) {
      continue;
    }

    FrameResult result = branches_[index]->Advance(context);
    if (std::holds_alternative<Suspended>(result)) {
      continue;
    }
    if (auto* completed = std::get_if<Completed>(&result); completed != nullptr) {
      if (compete_) {
        markCompleted();
        abandonBranches(context);
        return Completed{.value_ = std::move(completed->value_)};
      }

      results_[index] = std::move(completed->value_);
      ++completed_count_;
      continue;
    }

    auto failure = std::get<Failed>(result).failure_;
    if (!failure.composed_) {
      failure = composeFailure(*branches_[index], std::move(failure));
    }

    markFailed();
    abandonBranches(context);
    return Failed{.failure_ = std::move(failure)};
  }

  if (compete_ || completed_count_ < branches_.size()) {
    return Suspended{};
  }

  auto joined = joinAllForkResult(branches_, results_);
  if (auto* failure = std::get_if<ExecutionFailure>(&joined); failure != nullptr) {
    markFailed();
    abandonBranches(context);
    return Failed{.failure_ = std::move(*failure)};
  }

  markCompleted();
  return Completed{.value_ = std::move(std::get<StepDataValue>(joined))};
}

void ForkFrame::Abandon(FrameContext& context) {
  if (IsTerminal()) {
    return;
  }

  markAbandoned();
  abandonBranches(context);
}

} // namespace strij::nodeagent::task_handlers::openworkflow
