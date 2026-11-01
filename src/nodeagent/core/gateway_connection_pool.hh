#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/event/event_plane.pb.h"

namespace strij::io {
class OutboundMailbox;
} // namespace strij::io

namespace strij::nodeagent {

// The node's shared pool of live gateway connections. The child-forward path,
// the event bus, and the gateway relay all use the same pool.
class GatewayConnectionPool {
public:
  GatewayConnectionPool() = default;
  ~GatewayConnectionPool() = default;

  GatewayConnectionPool(const GatewayConnectionPool&) = delete;
  auto operator=(const GatewayConnectionPool&) -> GatewayConnectionPool& = delete;
  GatewayConnectionPool(GatewayConnectionPool&&) noexcept = delete;
  auto operator=(GatewayConnectionPool&&) noexcept -> GatewayConnectionPool& = delete;

  void Register(std::shared_ptr<io::OutboundMailbox> mailbox);
  [[nodiscard]] auto LiveCount() const -> std::size_t;
  // Round-robin over live connections. Null when none are live.
  auto Next() -> io::OutboundMailbox*;
  // Round-robin over live connections whose gateway announced an event plane.
  // When `mode` is non-null it reports that gateway's strongest delivery mode.
  auto NextEventCapable(event::DeliveryMode* mode = nullptr) -> io::OutboundMailbox*;
  // Enqueue `frame` to every live connection except `exclude`.
  void Broadcast(std::span<const std::byte> frame, io::OutboundMailbox* exclude);
  // Associate a gateway identity (and its event-plane delivery mode) with a
  // live connection. A duplicate identity replaces the previous mapping.
  void SetGatewayId(io::OutboundMailbox* mailbox, std::string gateway_id,
                    event::DeliveryMode delivery_mode = event::DELIVERY_MODE_BEST_EFFORT);
  auto Find(std::string_view gateway_id) -> io::OutboundMailbox*;

private:
  struct Entry {
    std::shared_ptr<io::OutboundMailbox> mailbox_;
    std::string gateway_id_;
    event::DeliveryMode delivery_mode_{event::DELIVERY_MODE_BEST_EFFORT};
    bool live_{true};
  };

  void prune();
  auto entries() -> std::vector<std::shared_ptr<Entry>>& { return entries_; }

  std::vector<std::shared_ptr<Entry>> entries_;
  std::size_t next_index_{0};
};

} // namespace strij::nodeagent
