#pragma once

#include <optional>
#include <string>
#include <utility>

#include "common/core/utils/jv.hh"
#include "openworkflow/value.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

// The single value that flows from step to step and serves as the jq subject.
// It is either JSON or opaque bytes: the JSON arm is populated only when the
// bytes parse as JSON, so a child returning HTML downgrades the chain rather
// than corrupting it. `Bytes()` always holds the exact value the step produced
// (the original bytes for a sniffed value, canonical JSON text for a DSL `Value`).
class DataChain {
public:
  DataChain();

  // Sniffs `bytes`: the JSON arm when `utils::Jv::Parse` accepts it, otherwise
  // the opaque arm (empty input is opaque).
  static auto FromBytes(std::string bytes) -> DataChain;

  static auto FromValue(::strij::openworkflow::Value value) -> DataChain;

  [[nodiscard]] auto IsJson() const -> bool { return json_.has_value(); }
  // Precondition: IsJson(). The parsed value, used as the expression subject.
  [[nodiscard]] auto Json() const -> const utils::Jv& { return *json_; }
  [[nodiscard]] auto Bytes() const -> const std::string& { return bytes_; }

  // The chain as a DSL `Value`: the parsed value when JSON, otherwise the
  // opaque bytes as a string value.
  [[nodiscard]] auto ToValue() const -> ::strij::openworkflow::Value;

private:
  DataChain(std::optional<utils::Jv> json, std::string bytes);

  std::optional<utils::Jv> json_;
  std::string bytes_;
};

} // namespace strij::nodeagent::task_handlers::openworkflow
