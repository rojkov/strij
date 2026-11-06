#pragma once

#include <cstddef>
#include <memory>

#include "absl/status/status.h"
#include "common/task/task.pb.h"
#include "nodeagent/core/gateway_connection_pool.hh"
#include "strij/nodeagent/child_task_forwarder.hh"

namespace strij::io {
class OutboundMailbox;
} // namespace strij::io

namespace strij::nodeagent {

// Node-global forwarder for child tasks that cannot be satisfied locally.
// Manages a pool of outbound gateway connections (registered via
// RegisterConnection) and round-robins kTaskSubmission frames across them.
class GatewayClient final : public ChildTaskForwarder {
public:
  GatewayClient() : connections_{owned_connections_} {}
  explicit GatewayClient(GatewayConnectionPool& connections) : connections_{connections} {}

  ~GatewayClient() override = default;

  GatewayClient(const GatewayClient&) = delete;
  auto operator=(const GatewayClient&) -> GatewayClient& = delete;
  GatewayClient(GatewayClient&&) noexcept = delete;
  auto operator=(GatewayClient&&) noexcept -> GatewayClient& = delete;

  // ChildTaskForwarder interface: forwards the task to a live gateway connection.
  // Returns a non-Ok status when no live connection is available.
  auto Forward(const task::Task& task) -> absl::Status override;

  // Registers a live outbound mailbox (from an accepted gateway connection).
  // The mailbox is unregistered automatically when the connection closes.
  void RegisterConnection(std::shared_ptr<io::OutboundMailbox> mailbox);

  // Number of currently live registered connections.
  [[nodiscard]] auto LiveCount() const -> size_t;

private:
  // Used only by the default constructor, so tests can construct a
  // GatewayClient without a framework-owned pool.
  GatewayConnectionPool owned_connections_;
  GatewayConnectionPool& connections_;
};

} // namespace strij::nodeagent
