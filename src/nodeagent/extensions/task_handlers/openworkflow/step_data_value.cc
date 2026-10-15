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
    return StepDataValue(std::move(*parsed), std::move(bytes));
  }
  return StepDataValue(std::nullopt, std::move(bytes));
}

auto StepDataValue::FromValue(::strij::openworkflow::Value value) -> StepDataValue {
  utils::Jv json = ::strij::openworkflow::ValueToJv(value);
  std::string bytes = json.Dump();
  return StepDataValue(std::move(json), std::move(bytes));
}

auto StepDataValue::ToValue() const -> ::strij::openworkflow::Value {
  if (json_.has_value()) {
    return ::strij::openworkflow::JvToValue(*json_);
  }
  ::strij::openworkflow::Value value{bytes_};
  return value;
}

} // namespace strij::nodeagent::task_handlers::openworkflow
