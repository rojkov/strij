#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/config/extensions.pb.h"
#include "common/core/utils/jv.hh"
#include "openworkflow/value.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

// The jq variable the interpreter binds to the DSL's workflow-scope `$context`.
// It is seeded empty and this slice never writes it, so it is always `{}`.
inline constexpr std::string_view kContextVariable = "context";

// Strict-mode recognition (design D5): a string is an expression if and only if
// it is entirely wrapped in `${ }`.
[[nodiscard]] auto IsExpression(std::string_view text) -> bool;
// True when a string carries a `${ }` wrapper that does not span the whole
// value (e.g. `"jobs/${ .id }"`). Such a value is rejected, never treated as
// literal text.
[[nodiscard]] auto HasPartialWrapper(std::string_view text) -> bool;
// The inner program of a whole-value `${ }` wrapper, trimmed.
[[nodiscard]] auto ExpressionProgram(std::string_view text) -> std::string;

// Recursively collects every expression program in `value`, descending into
// object members and array elements. Returns an error naming the wrapper rule
// when a partially-wrapped string is found at any depth.
auto CollectExpressions(const ::strij::openworkflow::Value& value, std::vector<std::string>& programs)
    -> absl::Status;

// Compiles and runs the configured evaluator against a JSON subject. Owns the
// evaluator selection (an `ExtensionConfig`) and re-resolves it through the
// shared loader for each compile and each run, matching the settled evaluator
// contract (the jq evaluator recompiles per run).
class ExpressionEngine {
public:
  explicit ExpressionEngine(config::ExtensionConfig evaluator) : evaluator_{std::move(evaluator)} {}

  // Validation: compiles `program`, discarding the result.
  auto Compile(std::string_view program) -> absl::Status;

  // Runs `program` against `subject` (subject = data chain, `$context` = `{}`)
  // and returns the single output value.
  auto Run(std::string_view program, const utils::Jv& subject) -> absl::StatusOr<utils::Jv>;

  // Validation over a value tree: compiles every expression it contains.
  auto Validate(const ::strij::openworkflow::Value& value) -> absl::Status;

  // Evaluates every expression in `value` against `subject`, preserving
  // literals and the surrounding structure.
  auto Evaluate(const ::strij::openworkflow::Value& value, const utils::Jv& subject)
      -> absl::StatusOr<::strij::openworkflow::Value>;

private:
  config::ExtensionConfig evaluator_;
};

// Renders a scalar result as a parameter string: a string is unquoted, a number
// is canonical, a boolean is `true`/`false`. Null and non-scalars are rejected.
auto RenderScalar(const utils::Jv& value) -> absl::StatusOr<std::string>;

} // namespace strij::nodeagent::task_handlers::openworkflow
