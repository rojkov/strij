#include "nodeagent/extensions/task_handlers/openworkflow/expression.hh"

#include <cctype>
#include <functional>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/loaders/evaluator_loader.hh"
#include "openworkflow/value_jv.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

auto IsExpression(std::string_view text) -> bool {
  return text.size() >= 3 && text.starts_with("${") && text.ends_with("}");
}

auto HasPartialWrapper(std::string_view text) -> bool {
  return text.find("${") != std::string_view::npos && !IsExpression(text);
}

auto ExpressionProgram(std::string_view text) -> std::string {
  std::string_view inner = text.substr(2, text.size() - 3);
  const auto is_space = [](char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
  };

  while (!inner.empty() && is_space(inner.front())) {
    inner.remove_prefix(1);
  }

  while (!inner.empty() && is_space(inner.back())) {
    inner.remove_suffix(1);
  }

  return std::string(inner);
}

namespace {

// Appends `nested`'s programs to `programs`.
void appendPrograms(std::vector<std::string>& programs,
                    const absl::StatusOr<std::vector<std::string>>& nested) {
  programs.insert(programs.end(), std::make_move_iterator(nested->begin()),
                  std::make_move_iterator(nested->end()));
}

} // namespace

auto CollectExpressions(const ::strij::openworkflow::Value& value)
    -> absl::StatusOr<std::vector<std::string>> {
  if (value.IsString()) {
    const std::string& text = value.AsString();
    if (IsExpression(text)) {
      return std::vector<std::string>{ExpressionProgram(text)};
    }

    if (HasPartialWrapper(text)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "the ${ } wrapper must span the whole value, but '", text, "' has text outside it"));
    }

    return std::vector<std::string>{};
  }

  if (value.IsArray()) {
    std::vector<std::string> programs;

    for (const ::strij::openworkflow::Value& element : value.AsArray()) {
      auto nested = CollectExpressions(element);
      if (!nested.ok()) {
        return nested.status();
      }

      appendPrograms(programs, nested);
    }

    return programs;
  }

  if (value.IsObject()) {
    std::vector<std::string> programs;
    for (const auto& [_key, member] : value.AsObject()) {
      auto nested = CollectExpressions(member);
      if (!nested.ok()) {
        return nested.status();
      }

      appendPrograms(programs, nested);
    }

    return programs;
  }

  return std::vector<std::string>{};
}

auto TypedSlotName(TypedSlot slot) -> std::string_view {
  switch (slot) {
  case TypedSlot::kIf:
    return "if";
  case TypedSlot::kInputFrom:
    return "input.from";
  case TypedSlot::kTaskOutput:
    return "output.as";
  case TypedSlot::kTaskExport:
    return "export.as";
  case TypedSlot::kDocumentOutput:
    return "the document's output.as";
  case TypedSlot::kSwitchWhen:
    return "a switch case's when";
  }

  return "expression";
}

namespace {

auto typedSlotVariables(TypedSlot slot) -> std::vector<std::string> {
  std::vector<std::string> variables{std::string(kContextVariable)};

  if (slot == TypedSlot::kTaskOutput || slot == TypedSlot::kTaskExport) {
    variables.emplace_back(kInputVariable);
    variables.emplace_back(kOutputVariable);
  }

  return variables;
}

auto collectTypedPrograms(const ::strij::openworkflow::Value& value, std::string_view slot,
                          std::vector<std::string>& programs) -> absl::Status {
  if (value.IsString()) {
    const std::string& text = value.AsString();
    if (IsExpression(text)) {
      programs.push_back(ExpressionProgram(text));
      return absl::OkStatus();
    }

    if (HasPartialWrapper(text)) {
      return absl::InvalidArgumentError(
          absl::StrCat("the ${ } wrapper must span the whole value in '", slot, "', but '", text,
                       "' has text outside it"));
    }

    return absl::InvalidArgumentError(absl::StrCat(
        "'", slot, "' must be a whole ${ } expression, but '", text, "' is literal text"));
  }

  if (value.IsArray()) {
    for (const ::strij::openworkflow::Value& element : value.AsArray()) {
      auto status = collectTypedPrograms(element, slot, programs);
      if (!status.ok()) {
        return status;
      }
    }

    return absl::OkStatus();
  }

  if (value.IsObject()) {
    for (const auto& [_key, member] : value.AsObject()) {
      auto status = collectTypedPrograms(member, slot, programs);
      if (!status.ok()) {
        return status;
      }
    }
  }

  return absl::OkStatus();
}

} // namespace

auto ExpressionEngine::Compile(std::string_view program) -> absl::Status {
  return compile(program, {std::string(kContextVariable)});
}

auto ExpressionEngine::compile(std::string_view program,
                               const std::vector<std::string>& variable_names) -> absl::Status {
  extensions::evaluators::EvaluatorDeps deps;
  auto evaluator = loaders::CreateEvaluator(evaluator_, deps, program, variable_names);
  if (!evaluator.ok()) {
    return evaluator.status();
  }

  return absl::OkStatus();
}

auto ExpressionEngine::Run(std::string_view program, const utils::Jv& subject,
                           const ExpressionArguments& arguments) -> absl::StatusOr<utils::Jv> {
  utils::Jv empty_context;
  const utils::Jv* context = arguments.context;
  if (context == nullptr) {
    auto parsed = utils::Jv::Parse("{}");
    if (!parsed.ok()) {
      return parsed.status();
    }

    empty_context = std::move(*parsed);
    context = &empty_context;
  }

  std::vector<utils::Jv> args;
  args.push_back(context->Copy());

  return run(program, subject, {std::string(kContextVariable)}, args);
}

auto ExpressionEngine::run(std::string_view program, const utils::Jv& subject,
                           const std::vector<std::string>& variable_names,
                           std::span<const utils::Jv> args) -> absl::StatusOr<utils::Jv> {
  extensions::evaluators::EvaluatorDeps deps;
  auto evaluator = loaders::CreateEvaluator(evaluator_, deps, program, variable_names);
  if (!evaluator.ok()) {
    return evaluator.status();
  }

  auto outputs = (*evaluator)->Run(subject, args);
  if (!outputs.ok()) {
    return outputs.status();
  }

  if (outputs->empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("expression '", program, "' produced no result"));
  }

  if (outputs->size() > 1) {
    return absl::InvalidArgumentError(absl::StrCat("expression '", program, "' produced ",
                                                   outputs->size(),
                                                   " results; exactly one is required"));
  }

  return std::move(outputs->front());
}

auto ExpressionEngine::Validate(const ::strij::openworkflow::Value& value) -> absl::Status {
  auto programs = CollectExpressions(value);
  if (!programs.ok()) {
    return programs.status();
  }

  for (const std::string& program : *programs) {
    auto status = Compile(program);
    if (!status.ok()) {
      return status;
    }
  }

  return absl::OkStatus();
}

auto ExpressionEngine::Evaluate(const ::strij::openworkflow::Value& value, const utils::Jv& subject,
                                const ExpressionArguments& arguments)
    -> absl::StatusOr<::strij::openworkflow::Value> {
  if (value.IsString()) {
    const std::string& text = value.AsString();

    if (IsExpression(text)) {
      auto result = Run(ExpressionProgram(text), subject, arguments);
      if (!result.ok()) {
        return result.status();
      }

      return ::strij::openworkflow::JvToValue(*result);
    }

    if (HasPartialWrapper(text)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "the ${ } wrapper must span the whole value, but '", text, "' has text outside it"));
    }

    return value;
  }

  if (value.IsArray()) {
    ::strij::openworkflow::Value::Array array;
    array.reserve(value.AsArray().size());

    for (const ::strij::openworkflow::Value& element : value.AsArray()) {
      auto evaluated = Evaluate(element, subject, arguments);
      if (!evaluated.ok()) {
        return evaluated.status();
      }

      array.push_back(std::move(*evaluated));
    }

    return ::strij::openworkflow::Value{std::move(array)};
  }

  if (value.IsObject()) {
    ::strij::openworkflow::Value::Object object;
    for (const auto& [key, member] : value.AsObject()) {
      auto evaluated = Evaluate(member, subject, arguments);
      if (!evaluated.ok()) {
        return evaluated.status();
      }

      object.emplace(key, std::move(*evaluated));
    }

    return ::strij::openworkflow::Value{std::move(object)};
  }

  return value;
}

auto ExpressionEngine::ValidateTypedSlot(const ::strij::openworkflow::Value& value, TypedSlot slot,
                                         const ExpressionArguments& /*arguments*/) -> absl::Status {
  std::vector<std::string> programs;
  auto status = collectTypedPrograms(value, TypedSlotName(slot), programs);
  if (!status.ok()) {
    return status;
  }

  const std::vector<std::string> variable_names = typedSlotVariables(slot);
  for (const std::string& program : programs) {
    status = compile(program, variable_names);
    if (!status.ok()) {
      return status;
    }
  }

  return absl::OkStatus();
}

auto ExpressionEngine::EvaluateTypedSlot(const ::strij::openworkflow::Value& value, TypedSlot slot,
                                         const utils::Jv& subject,
                                         const ExpressionArguments& arguments)
    -> absl::StatusOr<utils::Jv> {
  const std::vector<std::string> variable_names = typedSlotVariables(slot);

  utils::Jv empty_context;
  const utils::Jv* context = arguments.context;
  if (context == nullptr) {
    auto parsed = utils::Jv::Parse("{}");
    if (!parsed.ok()) {
      return parsed.status();
    }

    empty_context = std::move(*parsed);
    context = &empty_context;
  }

  std::vector<utils::Jv> args;
  args.push_back(context->Copy());

  if (slot == TypedSlot::kTaskOutput || slot == TypedSlot::kTaskExport) {
    if (arguments.input == nullptr) {
      return absl::InvalidArgumentError("the task input is not JSON");
    }
    if (arguments.output == nullptr) {
      return absl::InvalidArgumentError("the task output is not JSON");
    }

    args.push_back(arguments.input->Copy());
    args.push_back(arguments.output->Copy());
  }

  std::function<absl::StatusOr<::strij::openworkflow::Value>(const ::strij::openworkflow::Value&)>
      evaluate = [&](const ::strij::openworkflow::Value& node)
      -> absl::StatusOr<::strij::openworkflow::Value> {
    if (node.IsString()) {
      const std::string& text = node.AsString();
      if (!IsExpression(text)) {
        if (HasPartialWrapper(text)) {
          return absl::InvalidArgumentError(
              absl::StrCat("the ${ } wrapper must span the whole value in '", TypedSlotName(slot),
                           "', but '", text, "' has text outside it"));
        }

        return absl::InvalidArgumentError(
            absl::StrCat("'", TypedSlotName(slot), "' must be a whole ${ } expression"));
      }

      auto result = run(ExpressionProgram(text), subject, variable_names, args);
      if (!result.ok()) {
        return result.status();
      }

      return ::strij::openworkflow::JvToValue(*result);
    }

    if (node.IsArray()) {
      ::strij::openworkflow::Value::Array array;
      array.reserve(node.AsArray().size());
      for (const ::strij::openworkflow::Value& element : node.AsArray()) {
        auto evaluated = evaluate(element);
        if (!evaluated.ok()) {
          return evaluated.status();
        }

        array.push_back(std::move(*evaluated));
      }

      return ::strij::openworkflow::Value{std::move(array)};
    }

    if (node.IsObject()) {
      ::strij::openworkflow::Value::Object object;
      for (const auto& [key, member] : node.AsObject()) {
        auto evaluated = evaluate(member);
        if (!evaluated.ok()) {
          return evaluated.status();
        }

        object.emplace(key, std::move(*evaluated));
      }

      return ::strij::openworkflow::Value{std::move(object)};
    }

    return node;
  };

  auto evaluated = evaluate(value);
  if (!evaluated.ok()) {
    return evaluated.status();
  }

  return ::strij::openworkflow::ValueToJv(*evaluated);
}

auto RenderScalar(const utils::Jv& value) -> absl::StatusOr<std::string> {
  const ::strij::openworkflow::Value scalar = ::strij::openworkflow::JvToValue(value);
  if (scalar.IsString()) {
    return scalar.AsString();
  }

  if (scalar.IsBool() || scalar.IsInt() || scalar.IsDouble()) {
    return value.Dump();
  }

  if (scalar.IsNull()) {
    return absl::InvalidArgumentError("the expression result is null");
  }

  return absl::InvalidArgumentError("the expression result is not a scalar");
}

} // namespace strij::nodeagent::task_handlers::openworkflow
