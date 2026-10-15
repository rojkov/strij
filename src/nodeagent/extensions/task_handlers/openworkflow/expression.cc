#include "nodeagent/extensions/task_handlers/openworkflow/expression.hh"

#include <cctype>
#include <iterator>
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

auto ExpressionEngine::Compile(std::string_view program) -> absl::Status {
  extensions::evaluators::EvaluatorDeps deps;
  auto evaluator =
      loaders::CreateEvaluator(evaluator_, deps, program, {std::string(kContextVariable)});
  if (!evaluator.ok()) {
    return evaluator.status();
  }

  return absl::OkStatus();
}

auto ExpressionEngine::Run(std::string_view program, const utils::Jv& subject)
    -> absl::StatusOr<utils::Jv> {
  extensions::evaluators::EvaluatorDeps deps;
  auto evaluator =
      loaders::CreateEvaluator(evaluator_, deps, program, {std::string(kContextVariable)});
  if (!evaluator.ok()) {
    return evaluator.status();
  }

  auto context = utils::Jv::Parse("{}");
  if (!context.ok()) {
    return context.status();
  }

  std::vector<utils::Jv> args;
  args.push_back(std::move(*context));

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

auto ExpressionEngine::Evaluate(const ::strij::openworkflow::Value& value, const utils::Jv& subject)
    -> absl::StatusOr<::strij::openworkflow::Value> {
  if (value.IsString()) {
    const std::string& text = value.AsString();

    if (IsExpression(text)) {
      auto result = Run(ExpressionProgram(text), subject);
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
      auto evaluated = Evaluate(element, subject);
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
      auto evaluated = Evaluate(member, subject);
      if (!evaluated.ok()) {
        return evaluated.status();
      }

      object.emplace(key, std::move(*evaluated));
    }

    return ::strij::openworkflow::Value{std::move(object)};
  }

  return value;
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
