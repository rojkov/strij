#include "core/gateway/requirements_resolver.hh"

#include <string>
#include <string_view>

#include "core/logging/log.hh"
#include "core/node/capabilities.pb.h"
#include "google/protobuf/map.h"

namespace strij::gateway {

auto ParamsOnlyRequirementsResolver::Resolve(
    const FunctionRef& /*ref*/,
    const google::protobuf::Map<std::string, std::string>& parameters) const
    -> strij::node::ResourceRequirements {
  constexpr std::string_view kDotPrefix = "resources.";
  constexpr std::string_view kDashPrefix = "resources-";

  strij::node::ResourceRequirements requirements;
  auto* resources = requirements.mutable_resources();
  for (const auto& [key, value] : parameters) {
    std::string_view pool;
    if (key.starts_with(kDotPrefix)) {
      pool = std::string_view(key).substr(kDotPrefix.size());
    } else if (key.starts_with(kDashPrefix)) {
      pool = std::string_view(key).substr(kDashPrefix.size());
    } else {
      continue;
    }
    if (pool.empty()) {
      continue;
    }
    try {
      (*resources)[std::string(pool)] = std::stoull(value);
    } catch (const std::exception&) {
      LOG_WARNING("Ignoring unparseable resource amount for pool '{}': '{}'", pool, value);
    }
  }
  return requirements;
}

} // namespace strij::gateway
