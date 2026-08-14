#pragma once

#include <string>

#include "absl/status/statusor.h"
#include "core/config/nodeagent.pb.h"
#include "core/node/capabilities.pb.h"
#include "core/nodeagent/task_handler_manager.hh"

namespace strij::nodeagent {

inline constexpr uint32_t kCapabilityVersion = 1;
inline constexpr std::string_view kHeartbeatChannelKind = "heartbeat";
inline constexpr std::string_view kPushSchedulingProtocol = "push";

// Generates the node's stable (for the lifetime of the process) identity.
// Reuses the readable-id scheme from task-id generation; in v1 the identity is
// not persisted across restarts.
auto GenerateNodeId() -> std::string;

// Derives the NodeCapabilities advertisement from nodeagent config plus the
// handlers registered in TaskHandlerManager. Validates that:
//  - at least one ResourcePool is declared,
//  - every PoolReservation references a declared pool,
//  - every HandlerCapability names a registered task type.
// Returns InvalidArgumentError on any of the above failures.
auto BuildNodeCapabilities(const config::NodeAgentConfig& config,
                           const TaskHandlerManager& manager, const std::string& node_id)
    -> absl::StatusOr<strij::node::NodeCapabilities>;

} // namespace strij::nodeagent
