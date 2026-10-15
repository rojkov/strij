#include "nodeagent/extensions/task_handlers/openworkflow/call_strij.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "openworkflow/value_jv.hh"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

constexpr std::array<std::string_view, 3> kAcceptedKeys = {kTypeKey, kParametersKey,
                                                           kRequirementsKey};

auto findKey(const ::strij::openworkflow::Value::Object& object, std::string_view key)
    -> const ::strij::openworkflow::Value* {
  auto iter = object.find(std::string(key));
  return iter == object.end() ? nullptr : &iter->second;
}

// Renders one `parameters` value: a whole-value expression is evaluated against
// the data chain; a literal scalar is rendered canonically; an object or array
// is rejected.
auto projectParameter(std::string_view key, const ::strij::openworkflow::Value& member,
                      const utils::Jv* subject, ExpressionEngine& engine)
    -> absl::StatusOr<std::string> {
  if (member.IsString() && IsExpression(member.AsString())) {
    if (subject == nullptr) {
      return absl::InvalidArgumentError("the data subject is not JSON");
    }

    auto result = engine.Run(ExpressionProgram(member.AsString()), *subject);
    if (!result.ok()) {
      return result.status();
    }

    return RenderScalar(*result);
  }

  if (member.IsObject() || member.IsArray()) {
    return absl::InvalidArgumentError(
        absl::StrCat("parameter '", key, "' must be a scalar, not an object or array"));
  }

  return RenderScalar(::strij::openworkflow::ValueToJv(member));
}

auto projectParameters(const ::strij::openworkflow::Value& parameters, const utils::Jv* subject,
                       ExpressionEngine& engine, std::map<std::string, std::string>& out)
    -> absl::Status {
  for (const auto& [key, member] : parameters.AsObject()) {
    auto rendered = projectParameter(key, member, subject, engine);
    if (!rendered.ok()) {
      return rendered.status();
    }

    out.emplace(key, std::move(*rendered));
  }

  return absl::OkStatus();
}

auto projectRequirements(const ::strij::openworkflow::Value& requirements,
                         node::ResourceRequirements& out) -> absl::Status {
  for (const auto& [key, member] : requirements.AsObject()) {
    auto coerced = CoerceRequirement(key, member);
    if (!coerced.ok()) {
      return coerced.status();
    }

    (*out.mutable_resources())[key] = *coerced;
  }

  return absl::OkStatus();
}

} // namespace

auto ClassifyCallTarget(std::string_view target, const ::strij::openworkflow::Document& document)
    -> CallTargetKind {
  if (target == "strij") {
    return CallTargetKind::kStrij;
  }

  static constexpr std::array<std::string_view, 5> kReserved = {"http", "openapi", "asyncapi",
                                                                "grpc", "mcp"};
  for (std::string_view reserved : kReserved) {
    if (target == reserved) {
      return CallTargetKind::kReserved;
    }
  }

  if (document.use_.has_value() && document.use_->functions_.contains(std::string(target))) {
    return CallTargetKind::kDeclaredFunction;
  }

  return CallTargetKind::kUnknown;
}

auto CoerceRequirement(std::string_view name, const ::strij::openworkflow::Value& amount)
    -> absl::StatusOr<std::uint64_t> {
  if (amount.IsString()) {
    const std::string& text = amount.AsString();
    if (IsExpression(text)) {
      return absl::InvalidArgumentError(
          absl::StrCat("requirement '", name, "' does not admit expressions"));
    }

    return absl::InvalidArgumentError(
        absl::StrCat("requirement '", name, "' value '", text,
                     "' is not an integer; unit suffixes are not accepted"));
  }

  if (amount.IsInt()) {
    const std::int64_t value = amount.AsInt();
    if (value < 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("requirement '", name, "' value ", value, " must be non-negative"));
    }

    return static_cast<std::uint64_t>(value);
  }

  if (amount.IsDouble()) {
    const double value = amount.AsDouble();
    if (!std::isfinite(value) || std::floor(value) != value) {
      return absl::InvalidArgumentError(
          absl::StrCat("requirement '", name, "' value ", value, " is not an integer"));
    }

    if (value < 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("requirement '", name, "' value ", value, " must be non-negative"));
    }

    if (value > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
      return absl::InvalidArgumentError(
          absl::StrCat("requirement '", name, "' value ", value, " is out of range"));
    }

    return static_cast<std::uint64_t>(value);
  }

  return absl::InvalidArgumentError(
      absl::StrCat("requirement '", name, "' value is not an integer"));
}

auto ValidateCallArguments(const ::strij::openworkflow::Value& with) -> absl::Status {
  if (!with.IsObject()) {
    return absl::InvalidArgumentError("call arguments ('with') must be an object");
  }

  const ::strij::openworkflow::Value::Object& object = with.AsObject();
  for (const auto& [key, member] : object) {
    const std::string_view key_view{key};
    const bool accepted =
        std::find(kAcceptedKeys.begin(), kAcceptedKeys.end(), key_view) != kAcceptedKeys.end();
    if (!accepted) {
      return absl::InvalidArgumentError(
          absl::StrCat("call arguments ('with') contain unknown key '", key,
                       "'; accepted keys are type, parameters, requirements"));
    }
  }

  const ::strij::openworkflow::Value* type = findKey(object, kTypeKey);
  if (type == nullptr) {
    return absl::InvalidArgumentError("call: strij requires 'type'");
  }

  if (type->IsString() && IsExpression(type->AsString())) {
    return absl::InvalidArgumentError("'type' does not admit expressions");
  }

  if (!type->IsString()) {
    return absl::InvalidArgumentError("'type' must be a string");
  }

  const ::strij::openworkflow::Value* parameters = findKey(object, kParametersKey);
  if (parameters != nullptr) {
    if (!parameters->IsObject()) {
      return absl::InvalidArgumentError("'parameters' must be an object");
    }

    for (const auto& [key, member] : parameters->AsObject()) {
      if (member.IsObject() || member.IsArray()) {
        return absl::InvalidArgumentError(
            absl::StrCat("parameter '", key, "' must be a scalar, not an object or array"));
      }
    }
  }

  const ::strij::openworkflow::Value* requirements = findKey(object, kRequirementsKey);
  if (requirements != nullptr) {
    if (!requirements->IsObject()) {
      return absl::InvalidArgumentError("'requirements' must be an object");
    }

    for (const auto& [key, member] : requirements->AsObject()) {
      auto coerced = CoerceRequirement(key, member);
      if (!coerced.ok()) {
        return coerced.status();
      }
    }
  }

  return absl::OkStatus();
}

auto ProjectCall(const ::strij::openworkflow::Value& with, const utils::Jv* subject,
                 ExpressionEngine& engine) -> absl::StatusOr<CallProjection> {
  const ::strij::openworkflow::Value::Object& object = with.AsObject();
  CallProjection projection;
  projection.type_ = findKey(object, kTypeKey)->AsString();

  const ::strij::openworkflow::Value* parameters = findKey(object, kParametersKey);
  if (parameters != nullptr) {
    auto status = projectParameters(*parameters, subject, engine, projection.parameters_);
    if (!status.ok()) {
      return status;
    }
  }

  const ::strij::openworkflow::Value* requirements = findKey(object, kRequirementsKey);
  if (requirements != nullptr) {
    auto status = projectRequirements(*requirements, projection.requirements_);
    if (!status.ok()) {
      return status;
    }
  }

  return projection;
}

} // namespace strij::nodeagent::task_handlers::openworkflow
