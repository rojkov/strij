#include "core/nodeagent/capabilities.hh"

#include <string>
#include <string_view>
#include <unordered_map>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "core/config/nodeagent.pb.h"
#include "core/node/capabilities.pb.h"
#include "core/nodeagent/task_handler_manager.hh"
#include "core/utils/task_id.hh"

namespace strij::nodeagent {

auto GenerateNodeId() -> std::string { return absl::StrCat("node-", utils::GenerateTaskId()); }

auto BuildNodeCapabilities(const config::NodeAgentConfig& config,
                           const TaskHandlerManager& manager, const std::string& node_id)
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

  for (const auto& handler_cap : config.handlers()) {
    if (manager.GetHandler(handler_cap.task_type()) == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("HandlerCapability names task type '", handler_cap.task_type(),
                       "' but no matching task handler is registered"));
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
  for (const auto& handler_cap : config.handlers()) {
    auto* out = caps.add_handlers();
    out->set_task_type(handler_cap.task_type());
    out->set_concurrency(handler_cap.concurrency());
    out->set_function_sourced(handler_cap.function_sourced());
    if (handler_cap.has_default_resources()) {
      out->mutable_default_resources()->CopyFrom(handler_cap.default_resources());
    }
  }

  caps.add_update_channels()->set_kind(std::string(kHeartbeatChannelKind));
  caps.add_scheduling_protocols()->set_name(std::string(kPushSchedulingProtocol));

  return caps;
}

} // namespace strij::nodeagent
