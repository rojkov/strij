#pragma once

#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "strij/nodeagent/function_resolver.hh"

namespace strij::nodeagent {

// v1 function resolver: the function reference is the executable path, returned
// unchanged. Consumed through the abstract strij::nodeagent::FunctionResolver
// contract.
class LocalFunctionResolver final : public FunctionResolver {
public:
  // FunctionResolver
  auto Resolve(std::string_view reference) -> absl::StatusOr<std::string> override;
};

} // namespace strij::nodeagent
