#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "absl/status/status.h"
#include "strij/common/pure.hh"

namespace strij::extensions {

class GatewayTransportReceiver {
public:
  GatewayTransportReceiver() = default;
  virtual ~GatewayTransportReceiver() = default;

  GatewayTransportReceiver(const GatewayTransportReceiver&) = delete;
  auto operator=(const GatewayTransportReceiver&) -> GatewayTransportReceiver& = delete;
  GatewayTransportReceiver(GatewayTransportReceiver&&) noexcept = delete;
  auto operator=(GatewayTransportReceiver&&) noexcept -> GatewayTransportReceiver& = delete;

  virtual void OnMessage(std::span<const std::byte> payload) PURE;
};

// A best-effort byte pipe between gateway processes. The transport is not
// responsible for discovery, membership, ordering, or source attribution.
class GatewayTransport {
public:
  GatewayTransport() = default;
  virtual ~GatewayTransport() = default;

  GatewayTransport(const GatewayTransport&) = delete;
  auto operator=(const GatewayTransport&) -> GatewayTransport& = delete;
  GatewayTransport(GatewayTransport&&) noexcept = delete;
  auto operator=(GatewayTransport&&) noexcept -> GatewayTransport& = delete;

  virtual auto Send(std::span<const std::string_view> peer_ids, std::span<const std::byte> payload)
      -> absl::Status PURE;
  virtual auto Broadcast(std::span<const std::byte> payload) -> absl::Status PURE;
  virtual void SetReceiver(GatewayTransportReceiver* receiver) PURE;
  [[nodiscard]] virtual auto MaxPayloadSize() const -> std::size_t PURE;
};

} // namespace strij::extensions
