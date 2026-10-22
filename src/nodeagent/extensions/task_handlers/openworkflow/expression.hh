#pragma once

#include <cstdint>
#include <span>
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
// It is seeded as an empty object and replaced only by a task's `export.as`.
inline constexpr std::string_view kContextVariable = "context";
// The jq variables bound only for task `output.as` and `export.as`: the shaped
// task input and the transformed task output.
inline constexpr std::string_view kInputVariable = "input";
inline constexpr std::string_view kOutputVariable = "output";

// The values an expression may reference by name. Slots that do not bind a
// value leave its pointer null; `Run` and `Evaluate` bind only `context`.
struct ExpressionArguments {
  const utils::Jv* context{nullptr};
  const utils::Jv* input{nullptr};
  const utils::Jv* output{nullptr};
};

// An expression-typed DSL slot. Unlike literal-capable value slots, every
// string in these trees must be a whole `${ }` expression.
enum class TypedSlot : std::uint8_t {
  kIf = 0,
  kInputFrom = 1,
  kTaskOutput = 2,
  kTaskExport = 3,
  kDocumentOutput = 4,
  kSwitchWhen = 5,
};

[[nodiscard]] auto TypedSlotName(TypedSlot slot) -> std::string_view;

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
auto CollectExpressions(const ::strij::openworkflow::Value& value)
    -> absl::StatusOr<std::vector<std::string>>;

// Compiles and runs the configured evaluator against a JSON subject. Owns the
// evaluator selection (an `ExtensionConfig`) and re-resolves it through the
// shared loader for each compile and each run, matching the settled evaluator
// contract (the jq evaluator recompiles per run).
class ExpressionEngine {
public:
  explicit ExpressionEngine(config::ExtensionConfig evaluator) : evaluator_{std::move(evaluator)} {}

  // Validation: compiles `program`, discarding the result.
  auto Compile(std::string_view program) -> absl::Status;

  // Runs `program` against `subject` and returns the single output value.
  // `arguments.context` is bound to `$context`; `$input`/`$output` are never
  // bound on this literal-capable path.
  auto Run(std::string_view program, const utils::Jv& subject,
           const ExpressionArguments& arguments = {}) -> absl::StatusOr<utils::Jv>;

  // Validation over a value tree: compiles every expression it contains.
  auto Validate(const ::strij::openworkflow::Value& value) -> absl::Status;

  // Evaluates every expression in `value` against `subject`, preserving
  // literals and the surrounding structure. Only `$context` is bound.
  auto Evaluate(const ::strij::openworkflow::Value& value, const utils::Jv& subject,
                const ExpressionArguments& arguments = {})
      -> absl::StatusOr<::strij::openworkflow::Value>;

  // Typed-slot validation: every string in `value` must be a whole `${ }`
  // expression, and every discovered program is compiled with the slot's
  // expression arguments.
  auto ValidateTypedSlot(const ::strij::openworkflow::Value& value, TypedSlot slot,
                         const ExpressionArguments& arguments = {}) -> absl::Status;

  // JSON-faithful typed-slot evaluation. `subject` is the expression subject;
  // the task-output and task-export slots additionally require `input` and
  // `output` (the transformed task input and the transformed task output).
  auto EvaluateTypedSlot(const ::strij::openworkflow::Value& value, TypedSlot slot,
                         const utils::Jv& subject, const ExpressionArguments& arguments = {})
      -> absl::StatusOr<utils::Jv>;

private:
  auto compile(std::string_view program, const std::vector<std::string>& variable_names)
      -> absl::Status;
  auto run(std::string_view program, const utils::Jv& subject,
           const std::vector<std::string>& variable_names, std::span<const utils::Jv> args)
      -> absl::StatusOr<utils::Jv>;

  config::ExtensionConfig evaluator_;
};

// Renders a scalar result as a parameter string: a string is unquoted, a number
// is canonical, a boolean is `true`/`false`. Null and non-scalars are rejected.
auto RenderScalar(const utils::Jv& value) -> absl::StatusOr<std::string>;

} // namespace strij::nodeagent::task_handlers::openworkflow
