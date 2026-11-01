#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "absl/status/status.h"
#include "common/core/io/tlv_frame.hh"
#include "strij/extensions/gateway_transport.hh"
#include "strij/gateway/node_directory.hh"

namespace strij::gateway {

// The v1 GatewayTransport: one hop through a live node, which forwards the
// opaque payload to the target gateway connections.
class GatewayRelayTransport final : public extensions::GatewayTransport {
public:
  explicit GatewayRelayTransport(NodeDirectory& directory) : directory_{directory} {}
  ~GatewayRelayTransport() override = default;

  GatewayRelayTransport(const GatewayRelayTransport&) = delete;
  auto operator=(const GatewayRelayTransport&) -> GatewayRelayTransport& = delete;
  GatewayRelayTransport(GatewayRelayTransport&&) noexcept = delete;
  auto operator=(GatewayRelayTransport&&) noexcept -> GatewayRelayTransport& = delete;

  auto Send(std::span<const std::string_view> peer_ids,
            std::span<const std::byte> payload) -> absl::Status override;
  auto Broadcast(std::span<const std::byte> payload) -> absl::Status override;
  // Two-phase wiring: the transport exists before the event-plane extension
  // (the receiver), so the extension attaches itself from its constructor
  // through deps.transport_.SetReceiver(this).
  void SetReceiver(extensions::GatewayTransportReceiver* receiver) override {
    receiver_ = receiver;
  }
  [[nodiscard]] auto MaxPayloadSize() const -> std::size_t override { return kMaxPayloadSize; }

  // Handles an inbound kPeerRelayDelivery frame and forwards its payload to the
  // configured receiver.
  auto HandleFrame(const io::TlvFrame& frame) -> absl::Status;

  static constexpr std::size_t kMaxPayloadSize = 1024 * 1024;

private:
  auto relay(std::span<const std::string_view> peer_ids, std::span<const std::byte> payload)
      -> absl::Status;

  NodeDirectory& directory_;
  extensions::GatewayTransportReceiver* receiver_{nullptr};
};

} // namespace strij::gateway
