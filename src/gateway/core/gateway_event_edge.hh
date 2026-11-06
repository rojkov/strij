#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "absl/status/status.h"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/event/cloudevent.pb.h"
#include "strij/gateway/event_plane.hh"

namespace strij::gateway {

// The gateway edge between node TLV frames and the configured event-plane
// extension. It owns the subscription-id-to-node mapping and implements the
// extension's local-delivery callback.
class GatewayEventEdge final : public EventPlaneHost {
public:
  GatewayEventEdge() = default;
  ~GatewayEventEdge() override;

  GatewayEventEdge(const GatewayEventEdge&) = delete;
  auto operator=(const GatewayEventEdge&) -> GatewayEventEdge& = delete;
  GatewayEventEdge(GatewayEventEdge&&) noexcept = delete;
  auto operator=(GatewayEventEdge&&) noexcept -> GatewayEventEdge& = delete;

  // TODO: smells like we have circular dependencies at init-time. Try to get rid of it.
  void SetPlane(EventPlane* plane) { plane_ = plane; }
  [[nodiscard]] auto Plane() const -> EventPlane* { return plane_; }

  auto HandlePublishFrame(const io::TlvFrame& frame) -> absl::Status;
  auto HandleSubscribeFrame(const io::TlvFrame& frame,
                            const std::shared_ptr<io::OutboundMailbox>& mailbox) -> absl::Status;
  auto HandleUnsubscribeFrame(const io::TlvFrame& frame) -> absl::Status;
  auto HandleAckFrame(const io::TlvFrame& frame) -> absl::Status;

  // EventPlaneHost
  void Deliver(std::string_view subscription_id, const event::CloudEvent& event,
               std::string_view token) override;

  [[nodiscard]] auto SubscriptionCount() const -> std::size_t { return subscriptions_.size(); }

private:
  struct Entry {
    std::string subscription_id_;
    std::shared_ptr<io::OutboundMailbox> mailbox_;
  };

  void ensureCloseWatched(const std::shared_ptr<io::OutboundMailbox>& mailbox);
  void onConnectionClosed(io::OutboundMailbox* mailbox);
  static void sendSubscriptionResult(const std::shared_ptr<io::OutboundMailbox>& mailbox,
                                     std::string_view subscription_id, bool accepted,
                                     std::string_view reason, event::DeliveryMode delivery_mode);

  struct CloseWatch {
    std::shared_ptr<io::OutboundMailbox> mailbox_;
    std::size_t token_;
  };

  std::unordered_map<std::string, Entry> subscriptions_;
  std::unordered_map<io::OutboundMailbox*, CloseWatch> close_watches_;
  EventPlane* plane_{nullptr};
};

} // namespace strij::gateway
