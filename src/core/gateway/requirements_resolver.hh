#pragma once

#include <string>

#include "core/node/capabilities.pb.h"
#include "google/protobuf/map.h"
#include "strij/common/pure.hh"

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
  RequirementsResolver() = default;
  virtual ~RequirementsResolver() = default;

  RequirementsResolver(const RequirementsResolver&) = delete;
  auto operator=(const RequirementsResolver&) -> RequirementsResolver& = delete;
  RequirementsResolver(RequirementsResolver&&) noexcept = delete;
  auto operator=(RequirementsResolver&&) noexcept -> RequirementsResolver& = delete;

  [[nodiscard]] virtual auto
  Resolve(const FunctionRef& ref,
          const google::protobuf::Map<std::string, std::string>& parameters) const
      -> node::ResourceRequirements PURE;
};

using RequirementsResolverPtr = std::unique_ptr<RequirementsResolver>;

// Reads resource entries from task parameters keyed "resources.{pool}" (also
// accepted with a dash separator, "resources-{pool}") and returns them as a
// ResourceRequirements map keyed by pool name. Returns an empty map when no
// resource entries are present. Unparseable amounts are ignored with a
// warning.
class ParamsOnlyRequirementsResolver final : public RequirementsResolver {
public:
  [[nodiscard]] auto
  Resolve(const FunctionRef& ref,
          const google::protobuf::Map<std::string, std::string>& parameters) const
      -> node::ResourceRequirements override;
};

} // namespace strij::gateway
