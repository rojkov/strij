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
using ::strij::openworkflow::ForBody;
using ::strij::openworkflow::ForkBody;
using ::strij::openworkflow::NamedTask;
using ::strij::openworkflow::SetBody;
using ::strij::openworkflow::SwitchBody;
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

// A task's flow directive, defaulting to `continue`. Returned as a view into the
// task so callers never bind a view to a temporary string.
auto stepThen(const NamedTask& step) -> std::string_view {
  return step.task_.then_.has_value() ? std::string_view(*step.task_.then_)
                                      : std::string_view("continue");
}

} // namespace

auto ResolveFlowDirective(std::span<const NamedTask> scope, std::string_view then)
    -> absl::StatusOr<FlowDirective> {
  if (then.empty() || then == "continue") {
    return FlowDirective{.kind_ = FlowKind::kContinue};
  }

  if (then == "exit") {
    return FlowDirective{.kind_ = FlowKind::kExit};
  }

  if (then == "end") {
    return FlowDirective{.kind_ = FlowKind::kEnd};
  }

  std::optional<std::size_t> target;
  for (std::size_t index = 0; index < scope.size(); ++index) {
    if (scope[index].name_ == then) {
      if (target.has_value()) {
        return absl::InvalidArgumentError(
            absl::StrCat("flow directive '", then, "' is ambiguous in its sequence"));
      }

      target = index;
    }
  }

  if (!target.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat("flow directive '", then, "' does not name a task in the same sequence"));
  }

  return FlowDirective{.kind_ = FlowKind::kJump, .target_ = *target};
}

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

auto CreateRootFrame(const ::strij::openworkflow::Document& document,
                     std::size_t for_iteration_budget) -> std::unique_ptr<SequenceFrame> {
  return std::make_unique<SequenceFrame>(std::span<const NamedTask>(document.do_), std::nullopt,
                                         kDefaultSynchronousTransitionBudget, nullptr,
                                         for_iteration_budget);
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
    child_ = std::make_unique<SequenceFrame>(std::span<const NamedTask>(do_body->do_), std::nullopt,
                                             kDefaultSynchronousTransitionBudget, scope_,
                                             for_iteration_budget_);
  } else if (const auto* fork_body = std::get_if<ForkBody>(&task.body_)) {
    child_ = std::make_unique<ForkFrame>(std::span<const NamedTask>(fork_body->branches_),
                                         fork_body->compete_, scope_, for_iteration_budget_);
  } else if (const auto* for_body = std::get_if<ForBody>(&task.body_)) {
    child_ = std::make_unique<ForFrame>(steps_[cursor_], *for_body, scope_, for_iteration_budget_);
  } else {
    assert(false);

    return;
  }

  assert(step_input_.has_value());
  // Keep the transformed input for `output.as`; give the child its own copy so
  // the two values cannot share mutable state.
  child_->Enter(step_input_->Copy());
}

void SequenceFrame::Suspend(std::string child_id) {
  assert(IsActive());
  assert(!waiting_child_id_.has_value());
  waiting_child_id_ = std::move(child_id);
}

void SequenceFrame::ApplyOutcome(ChildOutcome outcome, FrameContext& context) {
  assert(IsActive());
  assert(waiting_child_id_.has_value());
  assert(step_input_.has_value());
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

  const NamedTask& step = steps_[cursor_];
  StepDataValue raw_output = StepDataValue::FromBytes(std::move(outcome.body_));
  (void)applyCompletedStep(step, std::move(raw_output), context, stepThen(step));
}

auto SequenceFrame::applyCompletedStep(const NamedTask& step, StepDataValue raw_output,
                                       FrameContext& context, std::string_view then)
    -> std::optional<FrameResult> {
  assert(step_input_.has_value());

  auto transformed = context.TransformTaskOutput(step, *step_input_, raw_output, scope_.get());
  if (!transformed.ok()) {
    step_input_.reset();

    return failStep(task::TASK_STATUS_INTERNAL, step, std::string(transformed.status().message()));
  }

  const absl::Status exported =
      context.ExportTaskResult(step, *step_input_, *transformed, scope_.get());
  if (!exported.ok()) {
    step_input_.reset();

    return failStep(task::TASK_STATUS_INTERNAL, step, std::string(exported.message()));
  }

  data_ = std::move(*transformed);
  step_input_.reset();

  return applyFlowDirective(step, then);
}

auto SequenceFrame::applyFlowDirective(const NamedTask& step, std::string_view then)
    -> std::optional<FrameResult> {
  auto directive = ResolveFlowDirective(steps_, then);
  if (!directive.ok()) {
    return failStep(task::TASK_STATUS_INTERNAL, step, std::string(directive.status().message()));
  }

  switch (directive->kind_) {
  case FlowKind::kContinue:
    ++cursor_;
    return std::nullopt;
  case FlowKind::kJump:
    cursor_ = directive->target_;
    return std::nullopt;
  case FlowKind::kExit:
    pending_result_ = Completed{.value_ = data_.Copy()};
    markCompleted();
    return std::nullopt;
  case FlowKind::kEnd:
    pending_result_ = Ended{.value_ = data_.Copy()};
    markCompleted();
    return std::nullopt;
  }

  return std::nullopt;
}

auto SequenceFrame::terminalResult() -> std::optional<FrameResult> {
  if (IsFailed()) {
    assert(failure_.has_value());
    return Failed{.failure_ = *failure_};
  }

  if (IsAbandoned()) {
    return Suspended{};
  }

  if (IsCompleted()) {
    return Completed{.value_ = data_.Copy()};
  }

  return std::nullopt;
}

auto SequenceFrame::advanceCall(const NamedTask& step, FrameContext& context)
    -> std::optional<FrameResult> {
  assert(step_input_.has_value());

  const absl::Status status = context.ExecuteCall(*this, step, *step_input_, scope_.get());
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

  if (auto* ended = std::get_if<Ended>(&result); ended != nullptr) {
    // `end` skips this container's own `output.as` and `export.as`; the parent
    // sequence propagates it unchanged.
    return Ended{.value_ = std::move(ended->value_)};
  }

  assert(step_input_.has_value());
  const NamedTask& step = steps_[cursor_];
  StepDataValue raw_output = std::move(std::get<Completed>(result).value_);
  child_.reset();

  return applyCompletedStep(step, std::move(raw_output), context, stepThen(step));
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
  if (pending_result_.has_value()) {
    FrameResult result = std::move(*pending_result_);
    pending_result_.reset();

    return result;
  }

  if (auto terminal = terminalResult(); terminal.has_value()) {
    return std::move(*terminal);
  }

  assert(IsActive());

  // A synchronous stretch begins fresh on every resume. A loop that awaits a
  // child therefore never accumulates against this budget.
  transitions_ = 0;

  while (cursor_ < steps_.size()) {
    if (pending_result_.has_value()) {
      FrameResult result = std::move(*pending_result_);
      pending_result_.reset();

      return result;
    }

    if (waiting_child_id_.has_value()) {
      return Suspended{};
    }

    if (failure_.has_value()) {
      return storedFailureResult();
    }

    const NamedTask& step = steps_[cursor_];
    const Task& task = step.task_;

    if (++transitions_ > transition_budget_) {
      return failStep(task::TASK_STATUS_INTERNAL, step,
                      absl::StrCat("the sequence exceeded its synchronous step budget at step '",
                                   step.name_, "'"));
    }

    auto condition = context.EvaluateCondition(step, data_, scope_.get());
    if (!condition.ok()) {
      return failStep(task::TASK_STATUS_INTERNAL, step, std::string(condition.status().message()));
    }

    if (!*condition) {
      // Full short-circuit: no `input.from`, body, `then`, `output.as`, or
      // `export`; the data value is unchanged.
      ++cursor_;

      continue;
    }

    auto input = context.TransformTaskInput(step, data_, scope_.get());
    if (!input.ok()) {
      return failStep(task::TASK_STATUS_INTERNAL, step, std::string(input.status().message()));
    }

    step_input_ = std::move(*input);

    std::optional<FrameResult> step_result;
    if (std::holds_alternative<CallBody>(task.body_)) {
      step_result = advanceCall(step, context);
    } else if (std::holds_alternative<DoBody>(task.body_) ||
               std::holds_alternative<ForkBody>(task.body_) ||
               std::holds_alternative<ForBody>(task.body_)) {
      step_result = advanceContainer(context);
    } else if (std::holds_alternative<SetBody>(task.body_)) {
      auto set_value = context.EvaluateSet(step, *step_input_, scope_.get());
      if (!set_value.ok()) {
        step_result =
            failStep(task::TASK_STATUS_INTERNAL, step, std::string(set_value.status().message()));
      } else {
        step_result = applyCompletedStep(step, std::move(*set_value), context, stepThen(step));
      }
    } else if (std::holds_alternative<SwitchBody>(task.body_)) {
      auto selected = context.SelectSwitchDirective(step, *step_input_, scope_.get());
      if (!selected.ok()) {
        step_result =
            failStep(task::TASK_STATUS_INTERNAL, step, std::string(selected.status().message()));
      } else {
        std::string_view then = stepThen(step);
        if (selected->has_value()) {
          then = **selected;
        }

        step_result = applyCompletedStep(step, step_input_->Copy(), context, then);
      }
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

ForFrame::ForFrame(const NamedTask& task, const ForBody& body,
                   std::shared_ptr<const LexicalScope> scope, std::size_t iteration_budget)
    : task_{task}, body_{body}, scope_{std::move(scope)}, iteration_budget_{iteration_budget} {
  auto names = NormalizeLoopVariables(body.for_.each_, body.for_.at_);
  assert(names.ok());
  names_ = names.ok() ? std::move(*names) : LoopVariableNames{.each_ = "item", .at_ = "index"};
}

void ForFrame::Enter(StepDataValue data) {
  assert(IsPending());
  accumulator_ = std::move(data);
  markActive();
}

auto ForFrame::fail(std::string message) -> FrameResult {
  failure_ = ExecutionFailure{
      .status_ = task::TASK_STATUS_INTERNAL,
      .origin_ = nullptr,
      .step_ = task_.name_,
      .message_ = std::move(message),
      .composed_ = false,
  };

  markFailed();

  return Failed{.failure_ = *failure_};
}

auto ForFrame::startCollection(FrameContext& context) -> std::optional<FrameResult> {
  auto collection = context.EvaluateForIn(task_, accumulator_, scope_.get());
  if (!collection.ok()) {
    return fail(std::string(collection.status().message()));
  }

  if (!collection->IsJson()) {
    return fail("the for.in collection is not a JSON value");
  }

  const ::strij::openworkflow::Value value = collection->ToValue();
  if (value.IsNull()) {
    items_.clear();
  } else if (value.IsArray()) {
    items_ = value.AsArray();
  } else {
    return fail("the for.in collection is not an array or null");
  }

  started_ = true;

  return std::nullopt;
}

void ForFrame::beginIteration() {
  const ::strij::openworkflow::Value& element = items_[next_index_];
  StepDataValue item = StepDataValue::FromValue(element);
  auto index = utils::Jv::Parse(std::to_string(next_index_));
  assert(index.ok());

  iteration_scope_ = std::make_shared<LexicalScope>(LexicalScope{
      .parent_ = scope_,
      .each_name_ = names_.each_,
      .item_ = item.Json().Copy(),
      .at_name_ = names_.at_,
      .index_ = std::move(*index),
  });

  ++next_index_;
  body_frame_ = std::make_unique<SequenceFrame>(
      body_.do_, std::nullopt, kDefaultSynchronousTransitionBudget, iteration_scope_);
  body_frame_->Enter(accumulator_.Copy());
}

auto ForFrame::Advance(FrameContext& context) -> FrameResult {
  if (IsAbandoned()) {
    return Suspended{};
  }

  if (failure_.has_value()) {
    if (IsActive()) {
      markFailed();
    }

    return Failed{.failure_ = *failure_};
  }

  assert(IsActive());

  // A synchronous stretch begins fresh on every resume. A loop that awaits a
  // child therefore never accumulates against this budget.
  iterations_ = 0;

  if (!started_) {
    if (auto failure = startCollection(context); failure.has_value()) {
      return std::move(*failure);
    }
  }

  while (true) {
    if (body_frame_ != nullptr) {
      FrameResult result = body_frame_->Advance(context);

      if (std::holds_alternative<Suspended>(result)) {
        return Suspended{};
      }

      if (auto* failed = std::get_if<Failed>(&result); failed != nullptr) {
        // Keep the failed body alive: its failure origin may point into it.
        failure_ = std::move(failed->failure_);
        markFailed();

        return Failed{.failure_ = *failure_};
      }

      if (auto* ended = std::get_if<Ended>(&result); ended != nullptr) {
        body_frame_.reset();
        iteration_scope_.reset();
        markCompleted();

        return Ended{.value_ = std::move(ended->value_)};
      }

      accumulator_ = std::move(std::get<Completed>(result).value_);
      body_frame_.reset();

      if (body_.while_.has_value()) {
        assert(iteration_scope_ != nullptr);
        auto keep_going = context.EvaluateForWhile(task_, accumulator_, *iteration_scope_);

        if (!keep_going.ok()) {
          return fail(std::string(keep_going.status().message()));
        }

        if (!*keep_going) {
          iteration_scope_.reset();
          markCompleted();

          return Completed{.value_ = accumulator_.Copy()};
        }
      }

      iteration_scope_.reset();
    }

    if (next_index_ >= items_.size()) {
      markCompleted();

      return Completed{.value_ = accumulator_.Copy()};
    }

    if (++iterations_ > iteration_budget_) {
      return fail(absl::StrCat("the for task exceeded its synchronous iteration budget at '",
                               task_.name_, "'"));
    }

    beginIteration();
  }
}

void ForFrame::Abandon(FrameContext& context) {
  if (IsTerminal()) {
    return;
  }

  markAbandoned();

  if (body_frame_ != nullptr) {
    body_frame_->Abandon(context);
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
        SequenceFrame::Branch{.index_ = index, .name_ = branch_declarations_[index].name_},
        kDefaultSynchronousTransitionBudget, scope_, for_iteration_budget_);
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

    if (auto* ended = std::get_if<Ended>(&result); ended != nullptr) {
      // `end` concludes the workflow from any branch; the other branches are
      // abandoned and the fork's own step pipeline is skipped.
      markCompleted();
      abandonBranches(context);

      return Ended{.value_ = std::move(ended->value_)};
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
