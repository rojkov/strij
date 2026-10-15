#include "nodeagent/extensions/task_handlers/openworkflow/interpreter.hh"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/core/utils/jv.hh"
#include "common/core/utils/task_id.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/call_strij.hh"
#include "openworkflow/parser/parse.hh"
#include "openworkflow/value_jv.hh"
#include "strij/gateway/result_receiver_storage.hh"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

// The child receiver. It accumulates result chunks and, only on a terminal
// outcome (final Deliver or DeliverError), reports to the handler through the
// sink. Partial chunks never transition the step. It holds no interpreter
// pointer, so a late outcome after cancellation is dropped by the handler.
class ChildReceiver final : public gateway::ResultReceiver {
public:
  ChildReceiver(ChildOutcomeSink& sink, std::string parent_id, std::string child_id)
      : sink_{sink}, parent_id_{std::move(parent_id)}, child_id_{std::move(child_id)} {}

  void Deliver(std::span<const std::byte> value, bool is_final) override {
    if (delivered_) {
      return;
    }

    body_.append(std::bit_cast<const char*>(value.data()), value.size());

    if (is_final) {
      finish(false, {}, task::TASK_STATUS_OK);
    }
  }

  void DeliverError(std::string_view reason, task::TaskStatus status) override {
    if (delivered_) {
      return;
    }

    finish(true, std::string(reason), status);
  }

private:
  void finish(bool is_error, std::string reason, task::TaskStatus status) {
    delivered_ = true;

    ChildOutcome outcome{.is_error_ = is_error,
                         .body_ = std::move(body_),
                         .reason_ = std::move(reason),
                         .status_ = status};
    sink_.OnChildOutcome(parent_id_, child_id_, std::move(outcome));

    sink_.Reap();
  }

  ChildOutcomeSink& sink_;
  std::string parent_id_;
  std::string child_id_;
  std::string body_;
  bool delivered_ = false;
};

} // namespace

auto CollectSteps(const ::strij::openworkflow::Document& document)
    -> std::vector<const ::strij::openworkflow::NamedTask*> {
  std::vector<const ::strij::openworkflow::NamedTask*> ordered;
  const std::function<void(const ::strij::openworkflow::Tasks&)> append =
      [&](const ::strij::openworkflow::Tasks& tasks) {
        for (const ::strij::openworkflow::NamedTask& named : tasks) {
          if (std::holds_alternative<::strij::openworkflow::DoBody>(named.task_.body_)) {
            append(std::get<::strij::openworkflow::DoBody>(named.task_.body_).do_);
          } else {
            ordered.push_back(&named);
          }
        }
      };
  append(document.do_);
  return ordered;
}

auto ValidateDocument(const ::strij::openworkflow::Document& document,
                      const std::vector<const ::strij::openworkflow::NamedTask*>& steps,
                      ExpressionEngine& engine) -> std::optional<Failure> {
  for (const ::strij::openworkflow::NamedTask* named : steps) {
    const ::strij::openworkflow::Task& task = named->task_;
    if (!std::holds_alternative<::strij::openworkflow::CallBody>(task.body_)) {
      return Failure{Phase::kValidation, task::TASK_STATUS_UNIMPLEMENTED, named->name_,
                     "this task kind is not supported in the current workflow slice"};
    }
    const ::strij::openworkflow::CallBody& call =
        std::get<::strij::openworkflow::CallBody>(task.body_);
    switch (ClassifyCallTarget(call.call_, document)) {
    case CallTargetKind::kStrij:
      break;
    case CallTargetKind::kReserved:
      return Failure{
          Phase::kValidation, task::TASK_STATUS_UNIMPLEMENTED, named->name_,
          absl::StrCat("call target '", call.call_, "' is recognized but not yet supported")};
    case CallTargetKind::kDeclaredFunction:
      return Failure{Phase::kValidation, task::TASK_STATUS_UNIMPLEMENTED, named->name_,
                     absl::StrCat("call target '", call.call_,
                                  "' names a declared function, which is not supported")};
    case CallTargetKind::kUnknown:
      return Failure{Phase::kValidation, task::TASK_STATUS_MALFORMED_REQUEST, named->name_,
                     absl::StrCat("call target '", call.call_, "' is not recognized")};
    }

    // Compile expressions before shape checks: a malformed program nested deep
    // in a value tree must be reported as a syntax error, not masked by the
    // enclosing value's shape.
    auto syntax = engine.Validate(call.with_);
    if (!syntax.ok()) {
      return Failure{Phase::kValidation, task::TASK_STATUS_MALFORMED_REQUEST, named->name_,
                     std::string(syntax.message())};
    }
    auto arguments = ValidateCallArguments(call.with_);
    if (!arguments.ok()) {
      return Failure{Phase::kValidation, task::TASK_STATUS_MALFORMED_REQUEST, named->name_,
                     std::string(arguments.message())};
    }

    if (task.input_.has_value() && task.input_->from_.has_value()) {
      const auto& from = *task.input_->from_;
      const absl::Status input_status =
          std::holds_alternative<std::string>(from)
              ? engine.Validate(::strij::openworkflow::Value{std::get<std::string>(from)})
              : engine.Validate(std::get<::strij::openworkflow::Value>(from));
      if (!input_status.ok()) {
        return Failure{Phase::kValidation, task::TASK_STATUS_MALFORMED_REQUEST, named->name_,
                       std::string(input_status.message())};
      }
    }
  }
  return std::nullopt;
}

WorkflowInterpreter::WorkflowInterpreter(ChildTaskSubmitter& submitter,
                                         config::ExtensionConfig evaluator, ChildOutcomeSink& sink,
                                         std::string parent_id, Completion on_complete)
    : submitter_{submitter}, engine_{std::move(evaluator)}, sink_{sink},
      parent_id_{std::move(parent_id)}, on_complete_{std::move(on_complete)} {}

void WorkflowInterpreter::Start(std::string document_text) {
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

  steps_ = CollectSteps(*document_);
  auto failure = ValidateDocument(*document_, steps_, engine_);
  if (failure.has_value()) {
    fail(failure->status_, failure->step_, failure->message_);
    return;
  }

  if (document_->input_.has_value() && document_->input_->from_.has_value()) {
    auto null_subject = utils::Jv::Parse("null");
    if (!null_subject.ok()) {
      fail(task::TASK_STATUS_INTERNAL, "", null_subject.status().message());
      return;
    }
    auto seed = renderFrom(*document_->input_->from_, &*null_subject);
    if (!seed.ok()) {
      fail(task::TASK_STATUS_INTERNAL, "", seed.status().message());
      return;
    }
    data_ = StepDataValue::FromBytes(std::move(*seed));
  } else {
    data_ = StepDataValue();
  }

  step_index_ = 0;
  run();
}

void WorkflowInterpreter::OnChildOutcome(const std::string& child_id, ChildOutcome outcome) {
  if (finished_ || cancelled_) {
    return;
  }
  if (!waiting_child_id_.has_value() || *waiting_child_id_ != child_id) {
    return;
  }
  if (in_submit_) {
    pending_ = Pending{child_id, std::move(outcome)};
    return;
  }
  if (!applyOutcome(child_id, std::move(outcome))) {
    return;
  }
  ++step_index_;
  run();
}

void WorkflowInterpreter::Cancel() { cancelled_ = true; }

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

void WorkflowInterpreter::run() {
  for (;;) {
    if (finished_ || cancelled_) {
      return;
    }
    if (step_index_ >= steps_.size()) {
      succeed();
      return;
    }
    executeStep(*steps_[step_index_]);
    if (finished_ || cancelled_) {
      return;
    }
    if (!pending_.has_value()) {
      return;
    }
    Pending pending = std::move(*pending_);
    pending_.reset();
    if (!applyOutcome(pending.child_id_, std::move(pending.outcome_))) {
      return;
    }
    ++step_index_;
  }
}

void WorkflowInterpreter::executeStep(const ::strij::openworkflow::NamedTask& step) {
  const ::strij::openworkflow::Task& task = step.task_;
  const ::strij::openworkflow::CallBody& call =
      std::get<::strij::openworkflow::CallBody>(task.body_);

  // A step only needs a JSON subject when one of its expressions runs; a step
  // that merely passes opaque bytes through to its child is fine.
  const utils::Jv* subject = data_.IsJson() ? &data_.Json() : nullptr;
  auto projection = ProjectCall(call.with_, subject, engine_);
  if (!projection.ok()) {
    fail(task::TASK_STATUS_INTERNAL, step.name_, projection.status().message());
    return;
  }
  auto body = buildChildBody(task, subject);
  if (!body.ok()) {
    fail(task::TASK_STATUS_INTERNAL, step.name_, body.status().message());
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

  waiting_child_id_ = child_id;
  in_submit_ = true;
  submitter_.Submit(std::move(child), std::make_unique<ChildReceiver>(sink_, parent_id_, child_id));
  in_submit_ = false;
}

auto WorkflowInterpreter::applyOutcome(const std::string& child_id, ChildOutcome outcome) -> bool {
  waiting_child_id_.reset();
  if (outcome.is_error_) {
    const task::TaskStatus status =
        outcome.status_ == task::TASK_STATUS_OK ? task::TASK_STATUS_INTERNAL : outcome.status_;
    fail(status, currentStepName(), outcome.reason_);
    return false;
  }
  data_ = StepDataValue::FromBytes(std::move(outcome.body_));
  return true;
}

auto WorkflowInterpreter::buildChildBody(const ::strij::openworkflow::Task& task,
                                         const utils::Jv* subject) -> absl::StatusOr<std::string> {
  if (!task.input_.has_value() || !task.input_->from_.has_value()) {
    return data_.Bytes();
  }
  return renderFrom(*task.input_->from_, subject);
}

auto WorkflowInterpreter::renderFrom(
    const std::variant<std::string, ::strij::openworkflow::Value>& from, const utils::Jv* subject)
    -> absl::StatusOr<std::string> {
  if (std::holds_alternative<std::string>(from)) {
    const std::string& text = std::get<std::string>(from);
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
  const ::strij::openworkflow::Value& value = std::get<::strij::openworkflow::Value>(from);
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
  result.set_body(data_.Bytes());
  result.set_is_final(true);
  finished_ = true;
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
  finished_ = true;
  if (on_complete_) {
    on_complete_(std::move(result));
  }
}

auto WorkflowInterpreter::currentStepName() const -> std::string {
  if (step_index_ < steps_.size()) {
    return steps_[step_index_]->name_;
  }
  return {};
}

} // namespace strij::nodeagent::task_handlers::openworkflow
