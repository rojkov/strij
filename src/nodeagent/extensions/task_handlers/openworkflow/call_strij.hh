#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/core/utils/jv.hh"
#include "common/node/capabilities.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/expression.hh"
#include "openworkflow/document.hh"
#include "openworkflow/value.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

inline constexpr std::string_view kTypeKey = "type";
inline constexpr std::string_view kParametersKey = "parameters";
inline constexpr std::string_view kRequirementsKey = "requirements";

// How a `call:` target is classified (openworkflow-call-strij). The distinction
// matters so a typo is never reported as a missing capability.
enum class CallTargetKind : std::uint8_t {
  kStrij = 0,
  kReserved = 1,
  kDeclaredFunction = 2,
  kUnknown = 3,
};

auto ClassifyCallTarget(std::string_view target, const strij::openworkflow::Document& document)
    -> CallTargetKind;

// A `call: strij` projection ready to become a child `task::Task`.
struct CallProjection {
  std::string type_;
  std::map<std::string, std::string> parameters_;
  node::ResourceRequirements requirements_;
};

// Coerces one `requirements` amount to a non-negative `uint64` (spec
// openworkflow-call-strij): integers only, no unit suffixes, no silent
// truncation. Exposed for focused tests.
auto CoerceRequirement(std::string_view name, const ::strij::openworkflow::Value& amount)
    -> absl::StatusOr<std::uint64_t>;

// Validates the data-independent facts of a `call: strij` `with` object: it is
// an object, its key set is exactly the three accepted keys, `type` is present
// and not an expression, parameters are scalar-or-expression, and every
// requirement amount coerces. Expression syntax is compiled separately over the
// whole tree.
auto ValidateCallArguments(const ::strij::openworkflow::Value& with) -> absl::Status;

// Projects `with` onto a child task descriptor, evaluating whole-value
// parameter expressions against `subject` and rendering the results as strings.
// `subject` is null when the data chain is not JSON; a step with no expression
// parameters does not need it, but an expression parameter then fails.
auto ProjectCall(const ::strij::openworkflow::Value& with, const utils::Jv* subject,
                 ExpressionEngine& engine) -> absl::StatusOr<CallProjection>;

} // namespace strij::nodeagent::task_handlers::openworkflow
