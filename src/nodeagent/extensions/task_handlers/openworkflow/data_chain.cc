#include "nodeagent/extensions/task_handlers/openworkflow/data_chain.hh"

#include <optional>
#include <string>
#include <utility>

#include "common/core/utils/jv.hh"
#include "openworkflow/value.hh"
#include "openworkflow/value_jv.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

DataChain::DataChain(std::optional<utils::Jv> json, std::string bytes)
    : json_{std::move(json)}, bytes_{std::move(bytes)} {}

DataChain::DataChain() : DataChain(utils::Jv::Parse("null").value(), "null") {}

auto DataChain::FromBytes(std::string bytes) -> DataChain {
  auto parsed = utils::Jv::Parse(bytes);
  if (parsed.ok()) {
    return DataChain(std::move(*parsed), std::move(bytes));
  }
  return DataChain(std::nullopt, std::move(bytes));
}

auto DataChain::FromValue(::strij::openworkflow::Value value) -> DataChain {
  utils::Jv json = ::strij::openworkflow::ValueToJv(value);
  std::string bytes = json.Dump();
  return DataChain(std::move(json), std::move(bytes));
}

auto DataChain::ToValue() const -> ::strij::openworkflow::Value {
  if (json_.has_value()) {
    return ::strij::openworkflow::JvToValue(*json_);
  }
  ::strij::openworkflow::Value value{bytes_};
  return value;
}

} // namespace strij::nodeagent::task_handlers::openworkflow
