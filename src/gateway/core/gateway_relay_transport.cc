#include "gateway/core/gateway_relay_transport.hh"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "common/core/io/connection.hh"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/utils/task_id.hh"
#include "common/event/event_plane.pb.h"
#include "gateway/core/node.hh"

namespace strij::gateway {

GatewayRelayTransport::GatewayRelayTransport(NodeDirectory& directory)
    : next_mailbox_{[&directory]() -> io::OutboundMailbox* {
        Node* node = directory.GetNextNode();
        if (node == nullptr || node->GetConnection() == nullptr) {
          return nullptr;
        }
        return node->GetConnection()->Mailbox().get();
      }} {}

namespace {

auto asBytes(std::string_view text) -> std::span<const std::byte> {
  return std::as_bytes(std::span(text.data(), text.size()));
}

} // namespace

auto GatewayRelayTransport::Send(std::span<const std::string_view> peer_ids,
                                 std::span<const std::byte> payload) -> absl::Status {
  return relay(peer_ids, payload);
}

auto GatewayRelayTransport::Broadcast(std::span<const std::byte> payload) -> absl::Status {
  return relay({}, payload);
}

auto GatewayRelayTransport::relay(std::span<const std::string_view> peer_ids,
                                  std::span<const std::byte> payload) -> absl::Status {
  if (payload.size() > kMaxPayloadSize) {
    return absl::InvalidArgumentError("relay payload exceeds the maximum size");
  }

  io::OutboundMailbox* mailbox = next_mailbox_ ? next_mailbox_() : nullptr;
  if (mailbox == nullptr) {
    return absl::UnavailableError("no live node available for relay");
  }

  event::PeerRelay relay;
  for (const std::string_view peer_id : peer_ids) {
    relay.add_target_gateway_ids(std::string(peer_id));
  }
  relay.set_payload(std::string(reinterpret_cast<const char*>(payload.data()), payload.size()));

  std::string serialized;
  if (!relay.SerializeToString(&serialized)) {
    return absl::InternalError("failed to serialize PeerRelay");
  }

  mailbox->Enqueue(io::SerializeTlvFrame(io::TlvFrame::kPeerRelay, asBytes(serialized)));
  return absl::OkStatus();
}

auto MakeGatewayHelloFrame(std::string_view gateway_id, event::DeliveryMode delivery_mode)
    -> std::vector<std::byte> {
  event::GatewayHello hello;
  hello.set_gateway_id(std::string(gateway_id));
  hello.set_delivery_mode(delivery_mode);

  std::string serialized;
  if (!hello.SerializeToString(&serialized)) {
    return {};
  }

  return io::SerializeTlvFrame(io::TlvFrame::kGatewayHello, asBytes(serialized));
}

auto ResolveGatewayId(std::string_view configured) -> std::string {
  if (!configured.empty()) {
    return std::string(configured);
  }

  return "gateway-" + utils::GenerateTaskId();
}

auto BuildGatewayHello(std::string_view gateway_id, event::DeliveryMode delivery_mode)
    -> std::optional<std::vector<std::byte>> {
  if (gateway_id.empty()) {
    return std::nullopt;
  }

  std::vector<std::byte> frame = MakeGatewayHelloFrame(gateway_id, delivery_mode);
  if (frame.empty()) {
    return std::nullopt;
  }

  return frame;
}

auto GatewayRelayTransport::HandleFrame(const io::TlvFrame& frame) -> absl::Status {
  if (frame.type_id != io::TlvFrame::kPeerRelayDelivery) {
    return absl::NotFoundError("relay transport does not own this frame type");
  }

  event::PeerRelayDelivery delivery;
  if (!delivery.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                               static_cast<int>(frame.value.size()))) {
    return absl::InvalidArgumentError("malformed PeerRelayDelivery");
  }

  if (receiver_ != nullptr) {
    receiver_->OnMessage(
        std::as_bytes(std::span(delivery.payload().data(), delivery.payload().size())));
  }

  return absl::OkStatus();
}

} // namespace strij::gateway
