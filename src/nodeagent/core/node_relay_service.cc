#include "nodeagent/core/node_relay_service.hh"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/event/event_plane.pb.h"

namespace strij::nodeagent {

namespace {

auto asBytes(std::string_view text) -> std::span<const std::byte> {
  return std::as_bytes(std::span(text.data(), text.size()));
}

} // namespace

auto NodeRelayService::HandleFrame(const io::TlvFrame& frame, io::OutboundMailbox& source)
    -> absl::Status {
  switch (frame.type_id) {
  case io::TlvFrame::kGatewayHello: {
    event::GatewayHello hello;
    if (!hello.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                              static_cast<int>(frame.value.size()))) {
      return absl::InvalidArgumentError("malformed GatewayHello");
    }

    if (hello.gateway_id().empty()) {
      return absl::InvalidArgumentError("GatewayHello with empty gateway_id");
    }

    connections_.SetGatewayId(&source, hello.gateway_id());
    return absl::OkStatus();
  }
  case io::TlvFrame::kPeerRelay: {
    event::PeerRelay relay;
    if (!relay.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                              static_cast<int>(frame.value.size()))) {
      return absl::InvalidArgumentError("malformed PeerRelay");
    }

    if (relay.payload().size() > kMaxRelayPayload) {
      return absl::InvalidArgumentError("PeerRelay payload exceeds the limit");
    }

    event::PeerRelayDelivery delivery;
    delivery.set_payload(relay.payload());
    std::string serialized;
    if (!delivery.SerializeToString(&serialized)) {
      return absl::InternalError("failed to serialize PeerRelayDelivery");
    }

    auto out = io::SerializeTlvFrame(io::TlvFrame::kPeerRelayDelivery, asBytes(serialized));
    io::OutboundMailbox* source_mailbox = &source;

    if (relay.target_gateway_ids().empty()) {
      connections_.Broadcast(out, source_mailbox);
      return absl::OkStatus();
    }

    for (const std::string& target : relay.target_gateway_ids()) {
      io::OutboundMailbox* mailbox = connections_.Find(target);
      if (mailbox != nullptr && mailbox != source_mailbox) {
        mailbox->Enqueue(out);
      }
    }

    return absl::OkStatus();
  }
  default:
    return absl::NotFoundError("node relay does not own this frame type");
  }
}

} // namespace strij::nodeagent
