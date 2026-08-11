#pragma once

#include <string>

#include "core/node/capabilities.pb.h"
#include "google/protobuf/map.h"

namespace strij::gateway {

// Reference to a function: the task type plus the function identifier taken
// from task parameters["function"] (the function plane is a future seam).
struct FunctionRef {
  std::string type;
  std::string id;
};

// Resolves the hardware requirements a task needs before scheduling. v1 ships
// ParamsOnlyRequirementsResolver; a repository-backed implementation is future
// work (function plane).
class RequirementsResolver {
public:
  virtual ~RequirementsResolver() = default;

  virtual auto Resolve(
      const FunctionRef& ref,
      const google::protobuf::Map<std::string, std::string>& parameters) const
      -> strij::node::ResourceRequirements = 0;
};

// Reads resource entries from task parameters keyed "resources.{pool}" (also
// accepted with a dash separator, "resources-{pool}") and returns them as a
// ResourceRequirements map keyed by pool name. Returns an empty map when no
// resource entries are present. Unparseable amounts are ignored with a
// warning.
class ParamsOnlyRequirementsResolver final : public RequirementsResolver {
public:
  auto Resolve(const FunctionRef& ref,
               const google::protobuf::Map<std::string, std::string>& parameters) const
      -> strij::node::ResourceRequirements override;
};

} // namespace strij::gateway
