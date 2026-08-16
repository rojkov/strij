#include "core/nodeagent/capabilities.hh"

#include <cstdint>
#include <string>
#include <unordered_map>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "core/config/nodeagent.pb.h"
#include "core/extensions/extension_registry.hh"
#include "core/node/capabilities.pb.h"
#include "core/utils/task_id.hh"
#include "extensions/task_handlers/task_handlers.hh"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"

namespace strij::nodeagent {

auto GenerateNodeId() -> std::string { return absl::StrCat("node-", utils::GenerateTaskId()); }

auto BuildNodeCapabilities(const config::NodeAgentConfig& config, const std::string& node_id)
    -> absl::StatusOr<strij::node::NodeCapabilities> {
  if (config.pools().empty()) {
    return absl::InvalidArgumentError(
        "NodeAgentConfig.pools is empty: at least one ResourcePool must be configured");
  }

  std::unordered_map<std::string, uint64_t> declared_pools;
  for (const auto& pool : config.pools()) {
    declared_pools[pool.name()] = pool.total();
  }

  for (const auto& reservation : config.reservations()) {
    if (!declared_pools.contains(reservation.pool())) {
      return absl::InvalidArgumentError(absl::StrCat(
          "PoolReservation for task type '", reservation.task_type(),
          "' references undeclared pool '", reservation.pool(), "'"));
    }
  }

  strij::node::NodeCapabilities caps;
  caps.set_node_id(node_id);
  caps.set_address(
      absl::StrCat(config.tlv_listener().address(), ":", config.tlv_listener().port()));
  caps.set_capability_version(kCapabilityVersion);

  for (const auto& pool : config.pools()) {
    auto* out = caps.add_pools();
    out->set_name(pool.name());
    out->set_total(pool.total());
  }
  for (const auto& reservation : config.reservations()) {
    auto* out = caps.add_reservations();
    out->set_task_type(reservation.task_type());
    out->set_pool(reservation.pool());
    out->set_amount(reservation.amount());
  }

  // Derive the advertised per-type handler capabilities from the extension
  // configs: each entry must resolve to a registered factory, its typed_config
  // must unpack, and the operator-declared capacity is read via ParseConfig.
  auto& registry =
      strij::extensions::Registry<strij::extensions::TaskHandlerFactory>::instance();
  for (const auto& ext : config.task_handlers()) {
    auto* factory = registry.GetFactory(ext.name());
    if (factory == nullptr) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Task handler '", ext.name(),
          "' is not registered. Ensure the extension library is linked and the name matches a "
          "registered factory."));
    }

    ::google::protobuf::Any unpacked;
    unpacked.CopyFrom(ext.typed_config());
    auto config_msg = factory->CreateEmptyConfigProto();
    if (!unpacked.UnpackTo(config_msg.get())) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Failed to unpack typed_config for task handler '", ext.name(), "': unknown type '",
          unpacked.type_url(), "'"));
    }

    auto capacity_result = factory->ParseConfig(*config_msg);
    if (!capacity_result.ok()) {
      return absl::InvalidArgumentError(absl::StrCat("Failed to parse capacity for task handler '",
                                                     ext.name(), "': ",
                                                     capacity_result.status().message()));
    }

    auto* out = caps.add_handlers();
    out->set_task_type(factory->Name());
    out->set_concurrency(capacity_result.value().concurrency());
    if (capacity_result.value().has_default_resources()) {
      out->mutable_default_resources()->CopyFrom(capacity_result.value().default_resources());
    }
  }

  caps.add_update_channels()->set_kind(std::string(kHeartbeatChannelKind));
  caps.add_scheduling_protocols()->set_name(std::string(kPushSchedulingProtocol));

  return caps;
}

} // namespace strij::nodeagent
