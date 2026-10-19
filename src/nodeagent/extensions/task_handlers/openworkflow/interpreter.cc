#include "nodeagent/extensions/task_handlers/openworkflow/interpreter.hh"

#include <bit>
#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/config/extensions.pb.h"
#include "common/core/utils/jv.hh"
#include "common/core/utils/task_id.hh"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/call_strij.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/errors.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/expression.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/step_data_value.hh"
#include "openworkflow/document.hh"
#include "openworkflow/events.hh"
#include "openworkflow/parser/parse.hh"
#include "openworkflow/task.hh"
#include "openworkflow/value_jv.hh"
#include "strij/gateway/result_receiver_storage.hh"
#include "strij/nodeagent/child_task_submitter.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

using ::strij::openworkflow::CallBody;
using ::strij::openworkflow::DoBody;
using ::strij::openworkflow::Document;
using ::strij::openworkflow::ForkBody;
using ::strij::openworkflow::NamedTask;
using ::strij::openworkflow::Task;
using ::strij::openworkflow::Tasks;

namespace {

// The child result receiver. It accumulates result chunks and, only on a terminal
// outcome (final Deliver or DeliverError), reports it to the interpreter.
// Partial chunks never transition the step. It holds only a weak reference, so
// a late outcome after the run is gone (cancelled or completed) is dropped.
class ChildResultReceiver final : public gateway::ResultReceiver {
public:
  ChildResultReceiver(std::weak_ptr<WorkflowInterpreter> interpreter, std::string child_id)
      : interpreter_{std::move(interpreter)}, child_id_{std::move(child_id)} {}

  void Deliver(std::span<const std::byte> value, bool is_final) override {
    if (delivered_) {
      return;
    }

    body_.append(std::bit_cast<const char*>(value.data()), value.size());

    if (is_final) {
      finish({}, task::TASK_STATUS_OK);
    }
  }

  void DeliverError(std::string_view reason, task::TaskStatus status) override {
    assert(status != task::TASK_STATUS_OK);

    if (delivered_) {
      return;
    }

    finish(std::string(reason), status);
  }

private:
  void finish(std::string reason, task::TaskStatus status) {
    delivered_ = true;

    if (auto interpreter = interpreter_.lock()) {
      interpreter->OnChildOutcome(
          child_id_, {.body_ = std::move(body_), .reason_ = std::move(reason), .status_ = status});
    }
  }

  std::weak_ptr<WorkflowInterpreter> interpreter_;
  std::string child_id_;
  std::string body_;
  bool delivered_{false};
};

auto currentStepName(const SequenceFrame& sequence) -> std::string {
  if (sequence.Cursor() < sequence.Steps().size()) {
    return sequence.Steps()[sequence.Cursor()]->name_;
  }

  return {};
}

auto validateInputFrom(const Task& task, ExpressionEngine& engine) -> std::optional<Failure> {
  if (!task.input_.has_value() || !task.input_->from_.has_value()) {
    return std::nullopt;
  }

  const auto& from = *task.input_->from_;
  const absl::Status status =
      std::holds_alternative<std::string>(from)
          ? engine.Validate(::strij::openworkflow::Value{std::get<std::string>(from)})
          : engine.Validate(std::get<::strij::openworkflow::Value>(from));

  if (!status.ok()) {
    return Failure{.phase_ = Phase::kValidation,
                   .status_ = task::TASK_STATUS_MALFORMED_REQUEST,
                   .message_ = std::string(status.message())};
  }

  return std::nullopt;
}

auto validateTasks(const Tasks& tasks, const Document& document, ExpressionEngine& engine)
    -> std::optional<Failure>;

auto validateTask(const NamedTask& named, const Document& document, ExpressionEngine& engine)
    -> std::optional<Failure> {
  const Task& task = named.task_;

  if (const auto* do_body = std::get_if<DoBody>(&task.body_)) {
    if (auto failure = validateInputFrom(task, engine); failure.has_value()) {
      failure->step_ = named.name_;
      return failure;
    }

    return validateTasks(do_body->do_, document, engine);
  }

  if (const auto* fork_body = std::get_if<ForkBody>(&task.body_)) {
    if (fork_body->compete_ && fork_body->branches_.empty()) {
      return Failure{.phase_ = Phase::kValidation,
                     .status_ = task::TASK_STATUS_MALFORMED_REQUEST,
                     .step_ = named.name_,
                     .message_ = "a competing fork must declare at least one branch"};
    }

    if (auto failure = validateInputFrom(task, engine); failure.has_value()) {
      failure->step_ = named.name_;
      return failure;
    }

    return validateTasks(fork_body->branches_, document, engine);
  }

  if (!std::holds_alternative<CallBody>(task.body_)) {
    return Failure{.phase_ = Phase::kValidation,
                   .status_ = task::TASK_STATUS_UNIMPLEMENTED,
                   .step_ = named.name_,
                   .message_ = "this task kind is not supported in the current workflow slice"};
  }

  const auto& call = std::get<CallBody>(task.body_);

  switch (ClassifyCallTarget(call.call_, document)) {
  case CallTargetKind::kStrij:
    break;
  case CallTargetKind::kReserved:
    return Failure{.phase_ = Phase::kValidation,
                   .status_ = task::TASK_STATUS_UNIMPLEMENTED,
                   .step_ = named.name_,
                   .message_ = absl::StrCat("call target '", call.call_,
                                            "' is recognized but not yet supported")};
  case CallTargetKind::kDeclaredFunction:
    return Failure{.phase_ = Phase::kValidation,
                   .status_ = task::TASK_STATUS_UNIMPLEMENTED,
                   .step_ = named.name_,
                   .message_ = absl::StrCat("call target '", call.call_,
                                            "' names a declared function, which is not supported")};
  case CallTargetKind::kUnknown:
    return Failure{.phase_ = Phase::kValidation,
                   .status_ = task::TASK_STATUS_MALFORMED_REQUEST,
                   .step_ = named.name_,
                   .message_ = absl::StrCat("call target '", call.call_, "' is not recognized")};
  }

  // Compile expressions before shape checks: a malformed program nested deep
  // in a value tree must be reported as a syntax error, not masked by the
  // enclosing value's shape.
  auto syntax = engine.Validate(call.with_);
  if (!syntax.ok()) {
    return Failure{.phase_ = Phase::kValidation,
                   .status_ = task::TASK_STATUS_MALFORMED_REQUEST,
                   .step_ = named.name_,
                   .message_ = std::string(syntax.message())};
  }

  auto arguments = ValidateCallArguments(call.with_);
  if (!arguments.ok()) {
    return Failure{.phase_ = Phase::kValidation,
                   .status_ = task::TASK_STATUS_MALFORMED_REQUEST,
                   .step_ = named.name_,
                   .message_ = std::string(arguments.message())};
  }

  if (auto failure = validateInputFrom(task, engine); failure.has_value()) {
    failure->step_ = named.name_;

    return failure;
  }

  return std::nullopt;
}

auto validateTasks(const Tasks& tasks, const Document& document, ExpressionEngine& engine)
    -> std::optional<Failure> {
  for (const NamedTask& named : tasks) {
    if (auto failure = validateTask(named, document, engine); failure.has_value()) {
      return failure;
    }
  }

  return std::nullopt;
}

} // namespace

SequenceFrame::SequenceFrame(SequenceFrame* parent_sequence, ForkFrame* owner_fork,
                             std::size_t branch_index, std::string branch_name)
    : Frame(FrameKind::kSequence, parent_sequence), owner_fork_{owner_fork},
      branch_index_{branch_index}, branch_name_{std::move(branch_name)} {}

// A finished sequence can no longer advance: it either ran to completion or
// was abandoned.
auto SequenceFrame::IsFinished() const -> bool {
  return state_ == State::kCompleted || state_ == State::kAbandoned;
}

auto SequenceFrame::WaitingChildId() const -> const std::string& {
  assert(IsWaiting());

  return *waiting_child_id_;
}

void SequenceFrame::setState(State new_state, std::optional<std::string> child_id) {
  switch (new_state) {
  case State::kReady:
    // Entered from pending, or released after a child outcome.
    assert(state_ == State::kPending || state_ == State::kWaiting);
    assert(!child_id.has_value());
    waiting_child_id_.reset();
    break;
  case State::kWaiting:
    assert(state_ == State::kReady);
    assert(child_id.has_value());
    waiting_child_id_ = std::move(child_id);
    break;
  case State::kCompleted:
    assert(state_ == State::kReady);
    assert(!child_id.has_value());
    waiting_child_id_.reset();
    break;
  case State::kAbandoned:
    assert(!child_id.has_value());
    if (state_ == State::kCompleted || state_ == State::kAbandoned) {
      return;
    }
    assert(state_ == State::kPending || state_ == State::kReady || state_ == State::kWaiting);
    waiting_child_id_.reset();
    break;
  case State::kPending:
    assert(false);
    return;
  }
  state_ = new_state;
}

// The frame tree is built only here; the frames keep their data private.
class FrameBuilder final {
public:
  static auto build(const Document& document) -> std::unique_ptr<SequenceFrame>;

private:
  static auto buildSequence(const Tasks& tasks, SequenceFrame* parent_sequence,
                            ForkFrame* owner_fork, std::size_t branch_index,
                            std::string branch_name) -> std::unique_ptr<SequenceFrame>;
  static auto buildFork(const ForkBody& fork, SequenceFrame& parent) -> std::unique_ptr<ForkFrame>;
  static auto buildChildFor(SequenceFrame& sequence, std::size_t index) -> std::unique_ptr<Frame>;
};

auto BuildFrames(const Document& document) -> std::unique_ptr<SequenceFrame> {
  return FrameBuilder::build(document);
}

auto FrameBuilder::build(const Document& document) -> std::unique_ptr<SequenceFrame> {
  return buildSequence(document.do_, nullptr, nullptr, 0, std::string{});
}

auto FrameBuilder::buildFork(const ForkBody& fork, SequenceFrame& parent)
    -> std::unique_ptr<ForkFrame> {
  auto frame = std::make_unique<ForkFrame>(&parent, fork.compete_);
  frame->branch_results_.resize(fork.branches_.size());

  for (std::size_t index = 0; index < fork.branches_.size(); ++index) {
    const NamedTask& named = fork.branches_[index];
    auto branch = std::make_unique<SequenceFrame>(nullptr, frame.get(), index, named.name_);
    branch->steps_.push_back(&named);
    branch->children_.push_back(buildChildFor(*branch, 0));
    frame->branches_.push_back(std::move(branch));
  }

  return frame;
}

auto FrameBuilder::buildChildFor(SequenceFrame& sequence, std::size_t index)
    -> std::unique_ptr<Frame> {
  const Task& task = sequence.steps_[index]->task_;

  if (const auto* do_body = std::get_if<DoBody>(&task.body_)) {
    return buildSequence(do_body->do_, &sequence, nullptr, 0, std::string{});
  }

  if (const auto* fork_body = std::get_if<ForkBody>(&task.body_)) {
    return buildFork(*fork_body, sequence);
  }

  return nullptr;
}

auto FrameBuilder::buildSequence(const Tasks& tasks, SequenceFrame* parent_sequence,
                                 ForkFrame* owner_fork, std::size_t branch_index,
                                 std::string branch_name) -> std::unique_ptr<SequenceFrame> {
  auto sequence = std::make_unique<SequenceFrame>(parent_sequence, owner_fork, branch_index,
                                                  std::move(branch_name));
  size_t task_count = tasks.size();
  sequence->steps_.reserve(task_count);
  for (const NamedTask& named : tasks) {
    sequence->steps_.push_back(&named);
  }
  sequence->children_.resize(task_count);
  for (std::size_t index = 0; index < task_count; ++index) {
    sequence->children_[index] = buildChildFor(*sequence, index);
  }

  return sequence;
}

auto ValidateDocument(const Document& document, ExpressionEngine& engine)
    -> std::optional<Failure> {
  return validateTasks(document.do_, document, engine);
}

WorkflowInterpreter::WorkflowInterpreter(ChildTaskSubmitter& submitter,
                                         config::ExtensionConfig evaluator, std::string parent_id,
                                         Completion on_complete)
    : submitter_{submitter}, engine_{std::move(evaluator)}, parent_id_{std::move(parent_id)},
      on_complete_{std::move(on_complete)} {}

void WorkflowInterpreter::setState(State new_state) {
  switch (new_state) {
  case State::kReady:
    assert(state_ == State::kIdle || state_ == State::kAdvancing);
    break;
  case State::kAdvancing:
    assert(state_ == State::kReady);
    break;
  case State::kSucceeded:
    assert(state_ == State::kAdvancing);
    break;
  case State::kFailed:
    assert(state_ == State::kIdle || state_ == State::kReady || state_ == State::kAdvancing);
    break;
  case State::kCancelled:
    assert(!IsTerminalState(state_));
    break;
  case State::kIdle:
    assert(false);
    return;
  }
  state_ = new_state;
}

void WorkflowInterpreter::Start(std::string_view document_text, std::string_view invocation_input) {
  if (state_ != State::kIdle) {
    return;
  }

  auto parsed = ::strij::openworkflow::Parse(document_text);
  if (!parsed.has_value()) {
    fail(task::TASK_STATUS_MALFORMED_REQUEST, "",
         absl::StrCat("workflow document did not parse: ", parsed.error().Message()));

    return;
  }

  document_.emplace(std::move(*parsed));

  const absl::Status mode = validateMode();
  if (!mode.ok()) {
    fail(task::TASK_STATUS_MALFORMED_REQUEST, "", mode.message());

    return;
  }

  auto failure = ValidateDocument(*document_, engine_);
  if (failure.has_value()) {
    fail(failure->status_, failure->step_, failure->message_);

    return;
  }

  // The invocation input seeds the chain. An empty body is the same as no
  // input: JSON null, which is also the subject a document-level `input.from`
  // sees in that case.
  StepDataValue invocation_data_value;
  if (!invocation_input.empty()) {
    invocation_data_value = StepDataValue::FromBytes(std::string(invocation_input));
  }

  StepDataValue seed;
  if (document_->input_.has_value() && document_->input_->from_.has_value()) {
    const utils::Jv* subject =
        invocation_data_value.IsJson() ? &invocation_data_value.Json() : nullptr;
    auto rendered = renderFrom(*document_->input_->from_, subject);
    if (!rendered.ok()) {
      fail(task::TASK_STATUS_INTERNAL, "", rendered.status().message());

      return;
    }

    seed = StepDataValue::FromBytes(std::move(*rendered));
  } else {
    seed = std::move(invocation_data_value);
  }

  root_ = BuildFrames(*document_);
  root_->data_ = std::move(seed);
  root_->setState(SequenceFrame::State::kReady);
  setState(State::kReady);

  drive();
}

void WorkflowInterpreter::OnChildOutcome(const std::string& child_id, ChildOutcome outcome) {
  if (IsTerminalState(state_)) {
    return;
  }

  auto owner = owner_by_child_.find(child_id);
  if (owner == owner_by_child_.end()) {
    // Unknown or abandoned child: dropped.
    return;
  }

  if (state_ == State::kAdvancing) {
    pending_.insert_or_assign(child_id, std::move(outcome));

    return;
  }

  applyOutcome(*owner->second, child_id, std::move(outcome));
  drive();
}

void WorkflowInterpreter::Cancel() {
  if (!IsTerminalState(state_)) {
    setState(State::kCancelled);
  }
}

auto WorkflowInterpreter::validateMode() -> absl::Status {
  if (document_->evaluate_.has_value() && document_->evaluate_->mode_.has_value()) {
    const std::string& mode = *document_->evaluate_->mode_;
    if (mode != "strict") {
      return absl::InvalidArgumentError(
          absl::StrCat("evaluate mode '", mode, "' is not supported; only 'strict' is"));
    }
  }

  return absl::OkStatus();
}

void WorkflowInterpreter::drive() {
  if (state_ != State::kReady) {
    return;
  }

  setState(State::kAdvancing);
  advanceSequence(*root_);

  // Outcomes that arrived while some frame was inside `Submit` for another
  // child are drained here, after the advancement that recorded them. Unknown
  // (abandoned) ids are simply discarded.
  while (!IsTerminalState(state_) && !pending_.empty()) {
    auto node = pending_.extract(pending_.begin());
    auto owner = owner_by_child_.find(node.key());
    if (owner != owner_by_child_.end()) {
      applyOutcome(*owner->second, node.key(), std::move(node.mapped()));
      advanceSequence(*root_);
    }
  }

  if (!IsTerminalState(state_)) {
    setState(State::kReady);
  }
}

void WorkflowInterpreter::advanceSequence(SequenceFrame& sequence) {
  while (!IsTerminalState(state_) && !sequence.IsFinished()) {
    if (sequence.IsWaiting()) {
      return;
    }

    if (sequence.cursor_ >= sequence.steps_.size()) {
      completeSequence(sequence);

      return;
    }

    if (!advanceStep(sequence, *sequence.steps_[sequence.cursor_])) {
      return;
    }
  }
}

auto WorkflowInterpreter::advanceStep(SequenceFrame& sequence, const NamedTask& step) -> bool {
  const Task& task = step.task_;

  if (std::holds_alternative<CallBody>(task.body_)) {
    executeCall(sequence, step);

    return true;
  }

  if (std::holds_alternative<DoBody>(task.body_)) {
    auto* child = dynamic_cast<SequenceFrame*>(sequence.children_[sequence.cursor_].get());
    if (child == nullptr) {
      fail(task::TASK_STATUS_INTERNAL, step.name_, "the nested sequence frame is missing");

      return false;
    }

    if (child->IsPending()) {
      child->data_ = std::move(sequence.data_);
      child->setState(SequenceFrame::State::kReady);
    }

    advanceSequence(*child);

    return child->IsCompleted();
  }

  if (std::holds_alternative<ForkBody>(task.body_)) {
    auto* fork = dynamic_cast<ForkFrame*>(sequence.children_[sequence.cursor_].get());
    if (fork == nullptr) {
      fail(task::TASK_STATUS_INTERNAL, step.name_, "the fork frame is missing");

      return false;
    }

    if (fork->IsPending()) {
      fork->setState(ForkFrame::State::kRunning);
      for (auto& branch : fork->branches_) {
        branch->data_ = sequence.data_.Copy();
        branch->setState(SequenceFrame::State::kReady);
      }
    }

    advanceFork(*fork);

    return fork->IsResolved();
  }

  fail(task::TASK_STATUS_UNIMPLEMENTED, step.name_,
       "this task kind is not supported in the current workflow slice");

  return false;
}

void WorkflowInterpreter::advanceFork(ForkFrame& fork) {
  // An empty join-all fork is well defined: it joins to the empty array. An
  // empty competing fork is rejected during validation.
  if (!fork.IsResolved() && fork.branches_.empty()) {
    resolveFork(fork, StepDataValue::FromBytes("[]"));

    return;
  }

  for (auto& branch : fork.branches_) {
    if (IsTerminalState(state_) || fork.IsResolved()) {
      return;
    }

    if (branch->IsFinished()) {
      continue;
    }

    advanceSequence(*branch);
  }
}

void WorkflowInterpreter::completeSequence(SequenceFrame& sequence) {
  if (sequence.IsFinished()) {
    return;
  }

  sequence.setState(SequenceFrame::State::kCompleted);

  if (sequence.owner_fork_ != nullptr) {
    onBranchComplete(*sequence.owner_fork_, sequence);

    return;
  }

  if (SequenceFrame* parent = sequence.ParentSequence(); parent != nullptr) {
    parent->data_ = std::move(sequence.data_);
    ++parent->cursor_;

    return;
  }

  succeed();
}

void WorkflowInterpreter::onBranchComplete(ForkFrame& fork, SequenceFrame& branch) {
  if (fork.IsResolved()) {
    return;
  }

  fork.branch_results_[branch.branch_index_] = std::move(branch.data_);
  ++fork.completed_count_;

  if (fork.compete_) {
    resolveFork(fork, std::move(*fork.branch_results_[branch.branch_index_]));

    return;
  }

  if (fork.completed_count_ < fork.branches_.size()) {
    return;
  }

  std::string array = "[";
  for (std::size_t index = 0; index < fork.branches_.size(); ++index) {
    const StepDataValue& result = *fork.branch_results_[index];
    if (!result.IsJson()) {
      fail(task::TASK_STATUS_INTERNAL, "",
           absl::StrCat("branch '", fork.branches_[index]->branch_name_,
                        "': the branch result is not a JSON value"));

      return;
    }

    if (index > 0) {
      array.push_back(',');
    }
    array.append(result.Bytes());
  }
  array.push_back(']');

  resolveFork(fork, StepDataValue::FromBytes(std::move(array)));
}

void WorkflowInterpreter::resolveFork(ForkFrame& fork, StepDataValue value) {
  fork.setState(ForkFrame::State::kResolved);

  // Abandon the remaining branches: their children keep running, but their
  // outcomes no longer match an owner and are dropped. The walk descends into
  // nested sequences and forks so an abandoned branch's grandchildren are
  // abandoned too.
  for (auto& branch : fork.branches_) {
    abandonFrame(*branch);
  }

  SequenceFrame* parent = fork.ParentSequence();
  parent->data_ = std::move(value);
  ++parent->cursor_;
}

void WorkflowInterpreter::abandonFrame(Frame& frame) {
  if (auto* sequence = dynamic_cast<SequenceFrame*>(&frame); sequence != nullptr) {
    if (sequence->IsWaiting()) {
      owner_by_child_.erase(sequence->WaitingChildId());
    }
    sequence->setState(SequenceFrame::State::kAbandoned);

    for (auto& child : sequence->children_) {
      if (child != nullptr) {
        abandonFrame(*child);
      }
    }

    return;
  }

  auto* fork = dynamic_cast<ForkFrame*>(&frame);
  if (fork == nullptr) {
    return;
  }

  fork->setState(ForkFrame::State::kAbandoned);
  for (auto& branch : fork->branches_) {
    abandonFrame(*branch);
  }
}

void WorkflowInterpreter::executeCall(SequenceFrame& sequence, const NamedTask& step) {
  const Task& task = step.task_;
  const auto& call = std::get<CallBody>(task.body_);

  // A step only needs a JSON subject when one of its expressions runs; a step
  // that merely passes opaque bytes through to its child is fine.
  const utils::Jv* subject = sequence.data_.IsJson() ? &sequence.data_.Json() : nullptr;
  auto projection = ProjectCall(call.with_, subject, engine_);
  if (!projection.ok()) {
    failSequence(sequence, task::TASK_STATUS_INTERNAL, projection.status().message());

    return;
  }

  auto body = buildChildBody(task, sequence.data_, subject);
  if (!body.ok()) {
    failSequence(sequence, task::TASK_STATUS_INTERNAL, body.status().message());

    return;
  }

  const std::string child_id = utils::GenerateTaskId();
  task::Task child;
  child.set_id(child_id);
  child.set_type(projection->type_);

  for (const auto& [key, value] : projection->parameters_) {
    (*child.mutable_parameters())[key] = value;
  }

  *child.mutable_requirements() = projection->requirements_;
  child.set_body(*body);

  sequence.setState(SequenceFrame::State::kWaiting, child_id);
  owner_by_child_.insert_or_assign(child_id, &sequence);

  submitter_.Submit(std::move(child),
                    std::make_unique<ChildResultReceiver>(weak_from_this(), child_id));

  // A local child may complete before `Submit` returns. Drain only this
  // branch's entry here; outcomes belonging to other frames stay queued for the
  // driver so they cannot disturb this branch's advancement.
  auto pending = pending_.find(child_id);
  if (pending != pending_.end()) {
    ChildOutcome outcome = std::move(pending->second);
    pending_.erase(pending);
    applyOutcome(sequence, child_id, std::move(outcome));
  }
}

auto WorkflowInterpreter::applyOutcome(SequenceFrame& sequence, const std::string& child_id,
                                       ChildOutcome outcome) -> bool {
  owner_by_child_.erase(child_id);
  if (sequence.IsWaiting() && sequence.WaitingChildId() == child_id) {
    sequence.setState(SequenceFrame::State::kReady);
  }

  if (outcome.IsError()) {
    failSequence(sequence, outcome.status_, outcome.reason_);

    return false;
  }

  sequence.data_ = StepDataValue::FromBytes(std::move(outcome.body_));
  ++sequence.cursor_;

  return true;
}

auto WorkflowInterpreter::buildChildBody(const Task& task, const StepDataValue& data,
                                         const utils::Jv* subject) -> absl::StatusOr<std::string> {
  if (!task.input_.has_value() || !task.input_->from_.has_value()) {
    return data.Bytes();
  }

  return renderFrom(*task.input_->from_, subject);
}

auto WorkflowInterpreter::renderFrom(
    const std::variant<std::string, ::strij::openworkflow::Value>& from, const utils::Jv* subject)
    -> absl::StatusOr<std::string> {
  if (std::holds_alternative<std::string>(from)) {
    const auto& text = std::get<std::string>(from);
    if (IsExpression(text)) {
      if (subject == nullptr) {
        return absl::InvalidArgumentError("the data subject is not JSON");
      }

      auto result = engine_.Run(ExpressionProgram(text), *subject);
      if (!result.ok()) {
        return result.status();
      }

      const ::strij::openworkflow::Value value = ::strij::openworkflow::JvToValue(*result);
      if (value.IsString()) {
        return value.AsString();
      }

      return result->Dump();
    }

    if (HasPartialWrapper(text)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "the ${ } wrapper must span the whole value, but '", text, "' has text outside it"));
    }

    return text;
  }

  const auto& value = std::get<::strij::openworkflow::Value>(from);
  if (subject == nullptr) {
    auto programs = CollectExpressions(value);
    if (!programs.ok() || !programs->empty()) {
      return absl::InvalidArgumentError("the data subject is not JSON");
    }
  }

  if (subject != nullptr) {
    auto evaluated = engine_.Evaluate(value, *subject);
    if (!evaluated.ok()) {
      return evaluated.status();
    }

    return ::strij::openworkflow::ValueToJv(*evaluated).Dump();
  }

  auto null_subject = utils::Jv::Parse("null");
  if (!null_subject.ok()) {
    return null_subject.status();
  }

  auto evaluated = engine_.Evaluate(value, *null_subject);
  if (!evaluated.ok()) {
    return evaluated.status();
  }

  return ::strij::openworkflow::ValueToJv(*evaluated).Dump();
}

void WorkflowInterpreter::succeed() {
  task::TaskResult result;
  result.set_id(parent_id_);
  result.set_body(root_->data_.Bytes());
  result.set_is_final(true);
  setState(State::kSucceeded);

  if (on_complete_) {
    on_complete_(std::move(result));
  }
}

void WorkflowInterpreter::fail(task::TaskStatus status, std::string_view step,
                               std::string_view message) {
  task::TaskResult result;
  result.set_id(parent_id_);
  result.set_status(status);
  result.set_body(step.empty() ? message : absl::StrCat("step '", step, "': ", message));
  result.set_is_final(true);
  setState(State::kFailed);
  if (on_complete_) {
    on_complete_(std::move(result));
  }
}

void WorkflowInterpreter::failSequence(SequenceFrame& sequence, task::TaskStatus status,
                                       std::string_view message) {
  const SequenceFrame* branch = &sequence;
  while (branch->owner_fork_ == nullptr && branch->ParentSequence() != nullptr) {
    branch = branch->ParentSequence();
  }

  if (branch->owner_fork_ == nullptr) {
    fail(status, currentStepName(sequence), message);

    return;
  }

  std::string body = absl::StrCat("branch '", branch->branch_name_, "': ");
  const std::string step = currentStepName(sequence);
  if (branch != &sequence && !step.empty()) {
    absl::StrAppend(&body, "step '", step, "': ");
  }
  absl::StrAppend(&body, message);

  fail(status, "", body);
}

} // namespace strij::nodeagent::task_handlers::openworkflow
