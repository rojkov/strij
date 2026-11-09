#pragma once

#include <cstdint>
#include <optional>
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

// One lexical binding supplied by a frame's scope chain. `name_` is the bare jq
// variable name (without `$`). `value_` is null on validation-only paths.
struct ExpressionLocal {
  std::string_view name_;
  const utils::Jv* value_{nullptr};
};

// The values an expression may reference by name. Slots that do not bind a
// value leave its pointer null; `Run` and `Evaluate` bind only `context` plus
// any lexical locals. Locals are declared after the reserved variables and are
// deduplicated innermost-first, so an inner loop shadows an outer one.
struct ExpressionArguments {
  const utils::Jv* context{nullptr};
  const utils::Jv* input{nullptr};
  const utils::Jv* output{nullptr};
  std::span<const ExpressionLocal> locals{};
};

// The reserved variables a slot binds, in binding order.
enum class SlotVariables : std::uint8_t {
  kContext = 0,
  kContextInputOutput = 1,
};

// One DSL slot descriptor. `name_` is the display name used in errors,
// `variables_` selects the reserved bindings, and `literals_` decides whether a
// bare string is literal text (a value-construction slot) or a malformed
// document (an expression-only slot that must produce one typed value).
struct ExpressionSlot {
  std::string_view name_;
  SlotVariables variables_;
  bool literals_;
};

// Expression-only slots: their result must be a single typed value, so a bare
// string is never meaningful.
inline constexpr ExpressionSlot kIf{"if", SlotVariables::kContext, false};
inline constexpr ExpressionSlot kSwitchWhen{"a switch case's when", SlotVariables::kContext, false};
inline constexpr ExpressionSlot kForIn{"a for task's in", SlotVariables::kContext, false};
inline constexpr ExpressionSlot kForWhile{"a for task's while", SlotVariables::kContext, false};

// Value-construction slots: they build a value tree, so literal text is kept.
inline constexpr ExpressionSlot kInputFrom{"input.from", SlotVariables::kContext, true};
inline constexpr ExpressionSlot kTaskOutput{"output.as", SlotVariables::kContextInputOutput, true};
inline constexpr ExpressionSlot kTaskExport{"export.as", SlotVariables::kContextInputOutput, true};
inline constexpr ExpressionSlot kDocumentOutput{"the document's output.as", SlotVariables::kContext,
                                                true};

// The generic literal-capable slot used by `with`, `set`, `emit.data`, `until`
// filters, and the non-string form of `for.in`.
inline constexpr ExpressionSlot kValue{"value", SlotVariables::kContext, true};

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

  // Validation over a value tree: applies `slot`'s literal policy (an
  // expression-only slot rejects a bare string; a value-construction slot keeps
  // it literal) and compiles every discovered expression with the slot's
  // reserved variables plus the lexical locals.
  auto Validate(const ::strij::openworkflow::Value& value, const ExpressionSlot& slot,
                const ExpressionArguments& arguments = {}) -> absl::Status;

  // Evaluates every expression in `value` against `subject`, preserving
  // literals and the surrounding structure. `slot` selects the reserved
  // variables; `$context` and any lexical locals are always bound, and
  // `$input`/`$output` are bound when the slot declares them and the caller
  // supplies them.
  auto Evaluate(const ::strij::openworkflow::Value& value, const ExpressionSlot& slot,
                const utils::Jv& subject, const ExpressionArguments& arguments = {})
      -> absl::StatusOr<::strij::openworkflow::Value>;

private:
  auto compile(std::string_view program, const std::vector<std::string>& variable_names)
      -> absl::Status;
  auto run(std::string_view program, const utils::Jv& subject,
           const std::vector<std::string>& variable_names, std::span<const utils::Jv> args)
      -> absl::StatusOr<utils::Jv>;

  config::ExtensionConfig evaluator_;
};

// The normalized loop variable names for a `for` task. `each` and `at` are bare
// jq identifiers (no leading `$`), defaulted to `item` and `index`.
struct LoopVariableNames {
  std::string each_;
  std::string at_;
};

// Trims, defaults, strips one leading `$`, validates the identifier shape, and
// rejects equal or reserved (`context`, `input`, `output`) names.
auto NormalizeLoopVariables(const std::optional<std::string>& each,
                            const std::optional<std::string>& at)
    -> absl::StatusOr<LoopVariableNames>;

// Renders a scalar result as a parameter string: a string is unquoted, a number
// is canonical, a boolean is `true`/`false`. Null and non-scalars are rejected.
auto RenderScalar(const utils::Jv& value) -> absl::StatusOr<std::string>;

} // namespace strij::nodeagent::task_handlers::openworkflow
