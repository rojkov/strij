#include "nodeagent/extensions/task_handlers/openworkflow/step_data_value.hh"

#include <optional>
#include <string>
#include <utility>

#include "common/core/utils/jv.hh"
#include "openworkflow/value.hh"
#include "openworkflow/value_jv.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

StepDataValue::StepDataValue(std::optional<utils::Jv> json, std::string bytes)
    : json_{std::move(json)}, bytes_{std::move(bytes)} {}

StepDataValue::StepDataValue() : StepDataValue(utils::Jv::Parse("null").value(), "null") {}

auto StepDataValue::FromBytes(std::string bytes) -> StepDataValue {
  auto parsed = utils::Jv::Parse(bytes);
  if (parsed.ok()) {
    return {std::move(*parsed), std::move(bytes)};
  }

  return {std::nullopt, std::move(bytes)};
}

auto StepDataValue::FromValue(const ::strij::openworkflow::Value& value) -> StepDataValue {
  utils::Jv json = ::strij::openworkflow::ValueToJv(value);
  std::string bytes = json.Dump();

  return {std::move(json), std::move(bytes)};
}

auto StepDataValue::Copy() const -> StepDataValue {
  std::optional<utils::Jv> json;
  if (json_.has_value()) {
    json.emplace(json_->Copy());
  }

  return {std::move(json), bytes_};
}

auto StepDataValue::ToValue() const -> ::strij::openworkflow::Value {
  if (json_.has_value()) {
    return ::strij::openworkflow::JvToValue(*json_);
  }

  return {bytes_};
}

} // namespace strij::nodeagent::task_handlers::openworkflow
