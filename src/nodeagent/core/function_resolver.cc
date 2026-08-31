#include "nodeagent/core/function_resolver.hh"

#include <string>
#include <string_view>

#include "absl/status/status.h"

namespace strij::nodeagent {

auto LocalFunctionResolver::Resolve(std::string_view reference) -> absl::StatusOr<std::string> {
  if (reference.empty()) {
    return absl::InvalidArgumentError("empty function reference");
  }
  return std::string(reference);
}

} // namespace strij::nodeagent
