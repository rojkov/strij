#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "common/core/io/tlv_frame.hh"
#include "common/event/event_plane.pb.h"
#include "strij/gateway/gateway_transport.hh"
#include "strij/gateway/node_directory.hh"

namespace strij::io {

class OutboundMailbox;

} // namespace strij::io

namespace strij::gateway {

// The v1 GatewayTransport: one hop through a live node, which forwards the
// opaque payload to the target gateway connections.
class GatewayRelayTransport final : public GatewayTransport {
public:
  using MailboxProvider = std::function<io::OutboundMailbox*()>;

  explicit GatewayRelayTransport(NodeDirectory& directory);
  explicit GatewayRelayTransport(MailboxProvider next_mailbox)
      : next_mailbox_{std::move(next_mailbox)} {}
  ~GatewayRelayTransport() override = default;

  GatewayRelayTransport(const GatewayRelayTransport&) = delete;
  auto operator=(const GatewayRelayTransport&) -> GatewayRelayTransport& = delete;
  GatewayRelayTransport(GatewayRelayTransport&&) noexcept = delete;
  auto operator=(GatewayRelayTransport&&) noexcept -> GatewayRelayTransport& = delete;

  auto Send(std::span<const std::string_view> peer_ids, std::span<const std::byte> payload)
      -> absl::Status override;
  auto Broadcast(std::span<const std::byte> payload) -> absl::Status override;
  // Two-phase wiring: the transport exists before the event-plane extension
  // (the receiver), so the extension attaches itself from its constructor
  // through deps.transport_.SetReceiver(this).
  void SetReceiver(GatewayTransportReceiver* receiver) override { receiver_ = receiver; }
  [[nodiscard]] auto MaxPayloadSize() const -> std::size_t override { return kMaxPayloadSize; }

  // Handles an inbound kPeerRelayDelivery frame and forwards its payload to the
  // configured receiver.
  auto HandleFrame(const io::TlvFrame& frame) -> absl::Status;

  static constexpr std::size_t kMaxPayloadSize = 1024 * 1024;

private:
  auto relay(std::span<const std::string_view> peer_ids, std::span<const std::byte> payload)
      -> absl::Status;

  MailboxProvider next_mailbox_;
  GatewayTransportReceiver* receiver_{nullptr};
};

// Serializes the identity handshake a gateway writes on every node connection.
// The delivery mode is the strongest guarantee the gateway's event plane
// supports; the node uses it to resolve `auto` subscriptions.
auto MakeGatewayHelloFrame(std::string_view gateway_id, event::DeliveryMode delivery_mode)
    -> std::vector<std::byte>;

// Resolves the identity the gateway announces in the relay handshake. An empty
// configured id yields a generated, process-unique `gateway-<id>`.
auto ResolveGatewayId(std::string_view configured) -> std::string;

// Builds the handshake written on an accepted node connection. A gateway with
// no event plane has an empty id and returns `nullopt`, so the node treats the
// connection as event-incapable.
auto BuildGatewayHello(std::string_view gateway_id, event::DeliveryMode delivery_mode)
    -> std::optional<std::vector<std::byte>>;

} // namespace strij::gateway
