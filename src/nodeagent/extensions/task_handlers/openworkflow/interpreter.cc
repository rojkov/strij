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

  root_ = CreateRootFrame(*document_);
  root_->Enter(std::move(seed));
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

  SequenceFrame* frame = owner->second;
  owner_by_child_.erase(owner);
  frame->ApplyOutcome(std::move(outcome));
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

void WorkflowInterpreter::terminalize(FrameResult result) {
  if (auto* completed = std::get_if<Completed>(&result); completed != nullptr) {
    succeed(std::move(completed->value_));

    return;
  }

  if (auto* failed = std::get_if<Failed>(&result); failed != nullptr) {
    const ExecutionFailure& failure = failed->failure_;
    fail(failure.status_, failure.composed_ ? std::string_view{} : failure.step_, failure.message_);
  }
}

void WorkflowInterpreter::drive() {
  if (state_ != State::kReady) {
    return;
  }

  setState(State::kAdvancing);
  terminalize(root_->Advance(*this));

  // Outcomes that arrived while some frame was inside `Submit` for another
  // child are drained here, after the advancement that recorded them. Unknown
  // (abandoned) ids are simply discarded.
  while (!IsTerminalState(state_) && !pending_.empty()) {
    auto node = pending_.extract(pending_.begin());
    auto owner = owner_by_child_.find(node.key());
    if (owner != owner_by_child_.end()) {
      SequenceFrame* frame = owner->second;
      owner_by_child_.erase(owner);
      frame->ApplyOutcome(std::move(node.mapped()));
      terminalize(root_->Advance(*this));
    }
  }

  if (!IsTerminalState(state_)) {
    setState(State::kReady);
  }
}

void WorkflowInterpreter::ForgetChild(std::string_view child_id) {
  owner_by_child_.erase(std::string(child_id));
}

auto WorkflowInterpreter::ExecuteCall(SequenceFrame& owner, const NamedTask& step) -> absl::Status {
  const Task& task = step.task_;
  const auto& call = std::get<CallBody>(task.body_);

  // A step only needs a JSON subject when one of its expressions runs; a step
  // that merely passes opaque bytes through to its child is fine.
  const utils::Jv* subject = owner.Data().IsJson() ? &owner.Data().Json() : nullptr;
  auto projection = ProjectCall(call.with_, subject, engine_);
  if (!projection.ok()) {
    return projection.status();
  }

  auto body = buildChildBody(task, owner.Data(), subject);
  if (!body.ok()) {
    return body.status();
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

  owner.Suspend(child_id);
  owner_by_child_.insert_or_assign(child_id, &owner);

  submitter_.Submit(std::move(child),
                    std::make_unique<ChildResultReceiver>(weak_from_this(), child_id));

  // A local child may complete before `Submit` returns. Drain only this
  // branch's entry here; outcomes belonging to other frames stay queued for the
  // driver so they cannot disturb this branch's advancement.
  auto pending = pending_.find(child_id);
  if (pending != pending_.end()) {
    ChildOutcome outcome = std::move(pending->second);
    pending_.erase(pending);
    owner_by_child_.erase(child_id);
    owner.ApplyOutcome(std::move(outcome));
  }

  return absl::OkStatus();
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

void WorkflowInterpreter::succeed(StepDataValue value) {
  task::TaskResult result;
  result.set_id(parent_id_);
  result.set_body(value.Bytes());
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

} // namespace strij::nodeagent::task_handlers::openworkflow
