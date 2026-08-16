#pragma once

#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "core/config/nodeagent.pb.h"
#include "core/node/capabilities.pb.h"

namespace strij::nodeagent {

inline constexpr uint32_t kCapabilityVersion = 1;
inline constexpr std::string_view kHeartbeatChannelKind = "heartbeat";
inline constexpr std::string_view kPushSchedulingProtocol = "push";

// Generates the node's stable (for the lifetime of the process) identity.
// Reuses the readable-id scheme from task-id generation; in v1 the identity is
// not persisted across restarts.
auto GenerateNodeId() -> std::string;

// Derives the NodeCapabilities advertisement from nodeagent config. Handlers
// are derived from config.task_handlers: each entry must resolve to a
// registered TaskHandlerFactory whose typed_config unpacks, and the advertised
// capacity (task_type = factory name, concurrency, default_resources) is read
// via TaskHandlerFactory::ParseConfig. Validates that:
//  - at least one ResourcePool is declared,
//  - every PoolReservation references a declared pool,
//  - every task_handlers entry resolves to a registered task handler.
// Returns InvalidArgumentError on any of the above failures.
auto BuildNodeCapabilities(const config::NodeAgentConfig& config, const std::string& node_id)
    -> absl::StatusOr<strij::node::NodeCapabilities>;

} // namespace strij::nodeagent
