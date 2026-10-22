#include "nodeagent/extensions/task_handlers/openworkflow/interpreter.hh"

#include <bit>
#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
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
using ::strij::openworkflow::SetBody;
using ::strij::openworkflow::SwitchBody;
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

auto slotValue(const std::variant<std::string, ::strij::openworkflow::Value>& slot)
    -> ::strij::openworkflow::Value {
  if (std::holds_alternative<std::string>(slot)) {
    return ::strij::openworkflow::Value{std::get<std::string>(slot)};
  }

  return std::get<::strij::openworkflow::Value>(slot);
}

auto validateSlot(const std::variant<std::string, ::strij::openworkflow::Value>& slot,
                  TypedSlot kind, ExpressionEngine& engine) -> std::optional<Failure> {
  const absl::Status status = engine.ValidateTypedSlot(slotValue(slot), kind);
  if (!status.ok()) {
    return Failure{.phase_ = Phase::kValidation,
                   .status_ = task::TASK_STATUS_MALFORMED_REQUEST,
                   .message_ = std::string(status.message())};
  }

  return std::nullopt;
}

auto validateTaskSlots(const Task& task, ExpressionEngine& engine) -> std::optional<Failure> {
  if (task.if_.has_value()) {
    const absl::Status status =
        engine.ValidateTypedSlot(::strij::openworkflow::Value{*task.if_}, TypedSlot::kIf);
    if (!status.ok()) {
      return Failure{.phase_ = Phase::kValidation,
                     .status_ = task::TASK_STATUS_MALFORMED_REQUEST,
                     .message_ = std::string(status.message())};
    }
  }

  if (task.input_.has_value() && task.input_->from_.has_value()) {
    if (auto failure = validateSlot(*task.input_->from_, TypedSlot::kInputFrom, engine);
        failure.has_value()) {
      return failure;
    }
  }

  if (task.output_.has_value() && task.output_->as_.has_value()) {
    if (auto failure = validateSlot(*task.output_->as_, TypedSlot::kTaskOutput, engine);
        failure.has_value()) {
      return failure;
    }
  }

  if (task.export_.has_value() && task.export_->as_.has_value()) {
    if (auto failure = validateSlot(*task.export_->as_, TypedSlot::kTaskExport, engine);
        failure.has_value()) {
      return failure;
    }
  }

  return std::nullopt;
}

using Scope = std::span<const NamedTask>;

auto validateSequence(Scope scope, const Document& document, ExpressionEngine& engine,
                      bool inside_fork_branch) -> std::optional<Failure>;

auto malformedFlowDirective(Scope scope, std::string_view then, const NamedTask& named)
    -> std::optional<Failure> {
  auto directive = ResolveFlowDirective(scope, then);
  if (directive.ok()) {
    return std::nullopt;
  }

  return Failure{.phase_ = Phase::kValidation,
                 .status_ = task::TASK_STATUS_MALFORMED_REQUEST,
                 .step_ = named.name_,
                 .message_ = std::string(directive.status().message())};
}

auto validateSwitchBody(const NamedTask& named, const SwitchBody& switch_body,
                        ExpressionEngine& engine, Scope scope) -> std::optional<Failure> {
  // Compile every `when` before the shape checks, mirroring the call-argument
  // path so a malformed program is reported as a syntax error.
  for (const auto& switch_case : switch_body.cases_) {
    if (!switch_case.when_.has_value()) {
      continue;
    }

    if (auto failure = validateSlot(::strij::openworkflow::Value{*switch_case.when_},
                                    TypedSlot::kSwitchWhen, engine);
        failure.has_value()) {
      failure->step_ = named.name_;
      failure->message_ =
          absl::StrCat("switch case '", switch_case.name_, "': ", failure->message_);
      return failure;
    }
  }

  std::size_t default_count = 0;
  for (const auto& switch_case : switch_body.cases_) {
    if (!switch_case.then_.has_value() || switch_case.then_->empty()) {
      return Failure{.phase_ = Phase::kValidation,
                     .status_ = task::TASK_STATUS_MALFORMED_REQUEST,
                     .step_ = named.name_,
                     .message_ = absl::StrCat("switch case '", switch_case.name_,
                                              "' must declare a then directive")};
    }

    if (!switch_case.when_.has_value()) {
      ++default_count;
    }
  }

  if (default_count > 1) {
    return Failure{.phase_ = Phase::kValidation,
                   .status_ = task::TASK_STATUS_MALFORMED_REQUEST,
                   .step_ = named.name_,
                   .message_ = "a switch may declare at most one case without when"};
  }

  for (const auto& switch_case : switch_body.cases_) {
    if (auto failure = malformedFlowDirective(scope, *switch_case.then_, named);
        failure.has_value()) {
      return failure;
    }
  }

  return std::nullopt;
}

auto validateTask(const NamedTask& named, Scope scope, const Document& document,
                  ExpressionEngine& engine, bool inside_fork_branch) -> std::optional<Failure> {
  const Task& task = named.task_;

  // Fork branches run concurrently, so there is no defined way to combine
  // simultaneous context writes. Reject export there before executing anything.
  if (inside_fork_branch && task.export_.has_value()) {
    return Failure{.phase_ = Phase::kValidation,
                   .status_ = task::TASK_STATUS_UNIMPLEMENTED,
                   .step_ = named.name_,
                   .message_ = "export inside a fork branch is not supported"};
  }

  if (auto failure = validateTaskSlots(task, engine); failure.has_value()) {
    failure->step_ = named.name_;
    return failure;
  }

  if (task.then_.has_value()) {
    if (auto failure = malformedFlowDirective(scope, *task.then_, named); failure.has_value()) {
      return failure;
    }
  }

  if (const auto* do_body = std::get_if<DoBody>(&task.body_)) {
    return validateSequence(do_body->do_, document, engine, inside_fork_branch);
  }

  if (const auto* fork_body = std::get_if<ForkBody>(&task.body_)) {
    if (fork_body->compete_ && fork_body->branches_.empty()) {
      return Failure{.phase_ = Phase::kValidation,
                     .status_ = task::TASK_STATUS_MALFORMED_REQUEST,
                     .step_ = named.name_,
                     .message_ = "a competing fork must declare at least one branch"};
    }

    // Each branch is a one-task sequence at runtime, so it is its own scope.
    for (const NamedTask& branch : fork_body->branches_) {
      if (auto failure = validateSequence(Scope(&branch, 1), document, engine, true);
          failure.has_value()) {
        return failure;
      }
    }

    return std::nullopt;
  }

  if (const auto* set_body = std::get_if<SetBody>(&task.body_)) {
    const ::strij::openworkflow::Value data = slotValue(set_body->set_);
    const absl::Status status = engine.Validate(data);
    if (!status.ok()) {
      return Failure{.phase_ = Phase::kValidation,
                     .status_ = task::TASK_STATUS_MALFORMED_REQUEST,
                     .step_ = named.name_,
                     .message_ = std::string(status.message())};
    }

    if (data.IsArray()) {
      return Failure{.phase_ = Phase::kValidation,
                     .status_ = task::TASK_STATUS_MALFORMED_REQUEST,
                     .step_ = named.name_,
                     .message_ = "a set value must be an object or a single scalar"};
    }

    return std::nullopt;
  }

  if (const auto* switch_body = std::get_if<SwitchBody>(&task.body_)) {
    return validateSwitchBody(named, *switch_body, engine, scope);
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

  return std::nullopt;
}

auto validateSequence(Scope scope, const Document& document, ExpressionEngine& engine,
                      bool inside_fork_branch) -> std::optional<Failure> {
  std::unordered_set<std::string> names;
  for (const NamedTask& named : scope) {
    if (!names.insert(named.name_).second) {
      return Failure{.phase_ = Phase::kValidation,
                     .status_ = task::TASK_STATUS_MALFORMED_REQUEST,
                     .step_ = named.name_,
                     .message_ =
                         absl::StrCat("duplicate task name '", named.name_, "' in one sequence")};
    }
  }

  for (const NamedTask& named : scope) {
    if (auto failure = validateTask(named, scope, document, engine, inside_fork_branch);
        failure.has_value()) {
      return failure;
    }
  }

  return std::nullopt;
}

} // namespace

auto ValidateDocument(const Document& document, ExpressionEngine& engine)
    -> std::optional<Failure> {
  if (document.input_.has_value() && document.input_->from_.has_value()) {
    if (auto failure = validateSlot(*document.input_->from_, TypedSlot::kInputFrom, engine);
        failure.has_value()) {
      return failure;
    }
  }

  if (document.output_.has_value() && document.output_->as_.has_value()) {
    if (auto failure = validateSlot(*document.output_->as_, TypedSlot::kDocumentOutput, engine);
        failure.has_value()) {
      return failure;
    }
  }

  return validateSequence(document.do_, document, engine, false);
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

  auto context = utils::Jv::Parse("{}");
  if (!context.ok()) {
    fail(task::TASK_STATUS_INTERNAL, "", context.status().message());

    return;
  }
  context_ = std::move(*context);

  // The invocation input seeds the chain. An empty body is the same as no
  // input: JSON null, which is also the subject a document-level `input.from`
  // sees in that case.
  StepDataValue invocation_data_value;
  if (!invocation_input.empty()) {
    invocation_data_value = StepDataValue::FromBytes(std::string(invocation_input));
  }

  StepDataValue seed;
  if (document_->input_.has_value() && document_->input_->from_.has_value()) {
    auto transformed = transformInput(*document_->input_->from_, invocation_data_value);
    if (!transformed.ok()) {
      fail(task::TASK_STATUS_INTERNAL, "", transformed.status().message());

      return;
    }

    seed = std::move(*transformed);
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
  frame->ApplyOutcome(std::move(outcome), *this);
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
  StepDataValue* final_value = nullptr;
  if (auto* completed = std::get_if<Completed>(&result); completed != nullptr) {
    final_value = &completed->value_;
  } else if (auto* ended = std::get_if<Ended>(&result); ended != nullptr) {
    // `end` concludes the workflow but the document's own `output.as` still
    // shapes the final value.
    final_value = &ended->value_;
  }

  if (final_value != nullptr) {
    StepDataValue value = std::move(*final_value);

    if (document_->output_.has_value() && document_->output_->as_.has_value()) {
      if (!value.IsJson()) {
        fail(task::TASK_STATUS_INTERNAL, "", "the document output subject is not JSON");

        return;
      }

      auto transformed =
          engine_.EvaluateTypedSlot(slotValue(*document_->output_->as_), TypedSlot::kDocumentOutput,
                                    value.Json(), ExpressionArguments{.context = &context_});
      if (!transformed.ok()) {
        fail(task::TASK_STATUS_INTERNAL, "", transformed.status().message());

        return;
      }

      value = StepDataValue::FromBytes(transformed->Dump());
    }

    succeed(std::move(value));

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
      frame->ApplyOutcome(std::move(node.mapped()), *this);
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

auto WorkflowInterpreter::EvaluateCondition(const NamedTask& step, const StepDataValue& raw)
    -> absl::StatusOr<bool> {
  const Task& task = step.task_;
  if (!task.if_.has_value()) {
    return true;
  }

  if (!raw.IsJson()) {
    return absl::InvalidArgumentError("the condition subject is not JSON");
  }

  auto result = engine_.EvaluateTypedSlot(::strij::openworkflow::Value{*task.if_}, TypedSlot::kIf,
                                          raw.Json(), ExpressionArguments{.context = &context_});
  if (!result.ok()) {
    return result.status();
  }

  const ::strij::openworkflow::Value value = ::strij::openworkflow::JvToValue(*result);
  if (!value.IsBool()) {
    return absl::InvalidArgumentError("the condition result is not a boolean");
  }

  return value.AsBool();
}

auto WorkflowInterpreter::TransformTaskInput(const NamedTask& step, const StepDataValue& raw)
    -> absl::StatusOr<StepDataValue> {
  const Task& task = step.task_;
  if (!task.input_.has_value() || !task.input_->from_.has_value()) {
    return raw.Copy();
  }

  return transformInput(*task.input_->from_, raw);
}

auto WorkflowInterpreter::TransformTaskOutput(const NamedTask& step, const StepDataValue& input,
                                              const StepDataValue& raw_output)
    -> absl::StatusOr<StepDataValue> {
  const Task& task = step.task_;
  if (!task.output_.has_value() || !task.output_->as_.has_value()) {
    return raw_output.Copy();
  }

  return transformOutput(*task.output_->as_, input, raw_output);
}

auto WorkflowInterpreter::transformInput(
    const std::variant<std::string, ::strij::openworkflow::Value>& from_value,
    const StepDataValue& raw) -> absl::StatusOr<StepDataValue> {
  const ::strij::openworkflow::Value slot = slotValue(from_value);
  const utils::Jv* subject = raw.IsJson() ? &raw.Json() : nullptr;

  if (subject == nullptr) {
    // A literal-only transformation does not need a subject; any expression
    // over an opaque value must fail rather than read an empty value.
    auto programs = CollectExpressions(slot);
    if (!programs.ok() || !programs->empty()) {
      return absl::InvalidArgumentError("the data subject is not JSON");
    }
  }

  utils::Jv null_subject;
  if (subject == nullptr) {
    auto parsed = utils::Jv::Parse("null");
    if (!parsed.ok()) {
      return parsed.status();
    }

    null_subject = std::move(*parsed);
    subject = &null_subject;
  }

  auto result = engine_.EvaluateTypedSlot(slot, TypedSlot::kInputFrom, *subject,
                                          ExpressionArguments{.context = &context_});
  if (!result.ok()) {
    return result.status();
  }

  return StepDataValue::FromBytes(result->Dump());
}

auto WorkflowInterpreter::transformOutput(
    const std::variant<std::string, ::strij::openworkflow::Value>& as_value,
    const StepDataValue& input, const StepDataValue& raw_output) -> absl::StatusOr<StepDataValue> {
  if (!input.IsJson()) {
    return absl::InvalidArgumentError("the task input is not JSON");
  }

  if (!raw_output.IsJson()) {
    return absl::InvalidArgumentError("the output subject is not JSON");
  }

  auto result = engine_.EvaluateTypedSlot(
      slotValue(as_value), TypedSlot::kTaskOutput, raw_output.Json(),
      ExpressionArguments{
          .context = &context_, .input = &input.Json(), .output = &raw_output.Json()});
  if (!result.ok()) {
    return result.status();
  }

  return StepDataValue::FromBytes(result->Dump());
}

auto WorkflowInterpreter::ExportTaskResult(const NamedTask& step, const StepDataValue& input,
                                           const StepDataValue& transformed_output)
    -> absl::Status {
  const Task& task = step.task_;
  if (!task.export_.has_value() || !task.export_->as_.has_value()) {
    return absl::OkStatus();
  }

  if (!input.IsJson()) {
    return absl::InvalidArgumentError("the task input is not JSON");
  }

  if (!transformed_output.IsJson()) {
    return absl::InvalidArgumentError("the task output is not JSON");
  }

  auto result = engine_.EvaluateTypedSlot(
      slotValue(*task.export_->as_), TypedSlot::kTaskExport, transformed_output.Json(),
      ExpressionArguments{
          .context = &context_, .input = &input.Json(), .output = &transformed_output.Json()});
  if (!result.ok()) {
    return result.status();
  }

  if (!::strij::openworkflow::JvToValue(*result).IsObject()) {
    return absl::InvalidArgumentError("the exported context must be an object");
  }

  context_ = std::move(*result);

  return absl::OkStatus();
}

auto WorkflowInterpreter::EvaluateSet(const NamedTask& step, const StepDataValue& input)
    -> absl::StatusOr<StepDataValue> {
  const auto& set_body = std::get<SetBody>(step.task_.body_);
  const ::strij::openworkflow::Value value = slotValue(set_body.set_);
  const utils::Jv* subject = input.IsJson() ? &input.Json() : nullptr;

  utils::Jv null_subject;
  if (subject == nullptr) {
    // Literal-only sets need no subject; any expression over an opaque value
    // must fail rather than read an empty value.
    auto programs = CollectExpressions(value);
    if (!programs.ok() || !programs->empty()) {
      return absl::InvalidArgumentError("the data subject is not JSON");
    }

    auto parsed = utils::Jv::Parse("null");
    if (!parsed.ok()) {
      return parsed.status();
    }

    null_subject = std::move(*parsed);
    subject = &null_subject;
  }

  auto result = engine_.Evaluate(value, *subject, ExpressionArguments{.context = &context_});
  if (!result.ok()) {
    return result.status();
  }

  return StepDataValue::FromValue(*result);
}

auto WorkflowInterpreter::SelectSwitchDirective(const NamedTask& step, const StepDataValue& input)
    -> absl::StatusOr<std::optional<std::string>> {
  const auto& switch_body = std::get<SwitchBody>(step.task_.body_);
  const ::strij::openworkflow::NamedCase* fallback = nullptr;
  bool has_conditional = false;

  for (const auto& switch_case : switch_body.cases_) {
    if (switch_case.when_.has_value()) {
      has_conditional = true;
    } else if (fallback == nullptr) {
      fallback = &switch_case;
    }
  }

  if (!has_conditional) {
    return fallback == nullptr ? std::nullopt : std::optional<std::string>{fallback->then_};
  }

  if (!input.IsJson()) {
    return absl::InvalidArgumentError(
        absl::StrCat("switch '", step.name_, "': the condition subject is not JSON"));
  }

  for (const auto& switch_case : switch_body.cases_) {
    if (!switch_case.when_.has_value()) {
      continue;
    }

    auto result = engine_.EvaluateTypedSlot(::strij::openworkflow::Value{*switch_case.when_},
                                            TypedSlot::kSwitchWhen, input.Json(),
                                            ExpressionArguments{.context = &context_});
    if (!result.ok()) {
      return absl::InvalidArgumentError(absl::StrCat("switch '", step.name_, "': case '",
                                                     switch_case.name_,
                                                     "': ", result.status().message()));
    }

    const ::strij::openworkflow::Value value = ::strij::openworkflow::JvToValue(*result);
    if (!value.IsBool()) {
      return absl::InvalidArgumentError(absl::StrCat("switch '", step.name_, "': case '",
                                                     switch_case.name_,
                                                     "': the when result is not a boolean"));
    }

    if (value.AsBool()) {
      return switch_case.then_;
    }
  }

  return fallback == nullptr ? std::nullopt : std::optional<std::string>{fallback->then_};
}

auto WorkflowInterpreter::ExecuteCall(SequenceFrame& owner, const NamedTask& step,
                                      const StepDataValue& input) -> absl::Status {
  const Task& task = step.task_;
  const auto& call = std::get<CallBody>(task.body_);

  // The transformed input is the subject for the call's expressions and the
  // source of the child body. A step that merely passes opaque bytes through
  // to its child does not need a JSON subject.
  const utils::Jv* subject = input.IsJson() ? &input.Json() : nullptr;
  auto projection = ProjectCall(call.with_, subject, &context_, engine_);
  if (!projection.ok()) {
    return projection.status();
  }

  const std::string child_id = utils::GenerateTaskId();
  task::Task child;
  child.set_id(child_id);
  child.set_type(projection->type_);

  for (const auto& [key, value] : projection->parameters_) {
    (*child.mutable_parameters())[key] = value;
  }

  *child.mutable_requirements() = projection->requirements_;
  child.set_body(input.Bytes());

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
    owner.ApplyOutcome(std::move(outcome), *this);
  }

  return absl::OkStatus();
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
