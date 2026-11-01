#pragma once

#include <cstddef>

#include "absl/status/status.h"
#include "common/core/io/tlv_frame.hh"
#include "nodeagent/core/gateway_connection_pool.hh"

namespace strij::io {
class OutboundMailbox;
} // namespace strij::io

namespace strij::nodeagent {

// Node side of the v1 gateway relay. It learns gateway identities from
// kGatewayHello and forwards opaque kPeerRelay payloads to the other gateway
// connections.
class NodeRelayService {
public:
  explicit NodeRelayService(GatewayConnectionPool& connections) : connections_{connections} {}
  ~NodeRelayService() = default;

  NodeRelayService(const NodeRelayService&) = delete;
  auto operator=(const NodeRelayService&) -> NodeRelayService& = delete;
  NodeRelayService(NodeRelayService&&) noexcept = delete;
  auto operator=(NodeRelayService&&) noexcept -> NodeRelayService& = delete;

  auto HandleFrame(const io::TlvFrame& frame, io::OutboundMailbox& source) -> absl::Status;

  static constexpr std::size_t kMaxRelayPayload = 1024 * 1024;

private:
  GatewayConnectionPool& connections_;
};

} // namespace strij::nodeagent
