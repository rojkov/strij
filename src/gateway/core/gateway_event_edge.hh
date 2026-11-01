#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "absl/status/status.h"
#include "common/core/io/connection.hh"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/event/cloudevent.pb.h"
#include "strij/extensions/event_plane.hh"

namespace strij::gateway {

// The gateway edge between node TLV frames and the configured event-plane
// extension. It owns the subscription-id-to-node mapping and implements the
// extension's local-delivery callback.
class GatewayEventEdge final : public extensions::EventPlaneHost {
public:
  GatewayEventEdge() = default;
  ~GatewayEventEdge() override;

  GatewayEventEdge(const GatewayEventEdge&) = delete;
  auto operator=(const GatewayEventEdge&) -> GatewayEventEdge& = delete;
  GatewayEventEdge(GatewayEventEdge&&) noexcept = delete;
  auto operator=(GatewayEventEdge&&) noexcept -> GatewayEventEdge& = delete;

  void SetPlane(extensions::EventPlane* plane) { plane_ = plane; }
  [[nodiscard]] auto Plane() const -> extensions::EventPlane* { return plane_; }

  auto HandlePublishFrame(const io::TlvFrame& frame) -> absl::Status;
  auto HandleSubscribeFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status;
  auto HandleUnsubscribeFrame(const io::TlvFrame& frame) -> absl::Status;

  // extensions::EventPlaneHost
  void Deliver(std::string_view subscription_id, const event::CloudEvent& event,
               std::string_view token) override;

  [[nodiscard]] auto SubscriptionCount() const -> std::size_t { return subscriptions_.size(); }

private:
  struct Entry {
    std::string subscription_id;
    std::shared_ptr<io::OutboundMailbox> mailbox;
  };

  void ensureCloseWatched(const std::shared_ptr<io::OutboundMailbox>& mailbox);
  void onConnectionClosed(io::OutboundMailbox* mailbox);

  struct CloseWatch {
    std::shared_ptr<io::OutboundMailbox> mailbox;
    std::size_t token;
  };

  std::unordered_map<std::string, Entry> subscriptions_;
  std::unordered_map<io::OutboundMailbox*, CloseWatch> close_watches_;
  extensions::EventPlane* plane_{nullptr};
};

} // namespace strij::gateway
