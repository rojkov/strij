#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/event/event_plane.pb.h"
#include "gateway/core/gateway_relay_transport.hh"
#include "gtest/gtest.h"

namespace strij::gateway {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

struct CapturedMailbox {
  std::vector<std::vector<std::byte>> frames;
  std::shared_ptr<io::OutboundMailbox> mailbox;

  CapturedMailbox() {
    mailbox = std::make_shared<io::OutboundMailbox>(
        [this](std::vector<std::byte> frame) { frames.push_back(std::move(frame)); });
  }
};

class CapturingReceiver final : public extensions::GatewayTransportReceiver {
public:
  void OnMessage(std::span<const std::byte> payload) override {
    payload_.assign(reinterpret_cast<const char*>(payload.data()), payload.size());
  }

  std::string payload_;
};

struct ParsedFrame {
  uint8_t type_id{0};
  std::string value;
};

auto parseFrame(const std::vector<std::byte>& frame) -> ParsedFrame {
  EXPECT_GE(frame.size(), 5U);
  ParsedFrame parsed;
  parsed.type_id = std::to_integer<uint8_t>(frame[0]);
  const std::size_t length = (std::to_integer<std::size_t>(frame[1]) << 24) |
                             (std::to_integer<std::size_t>(frame[2]) << 16) |
                             (std::to_integer<std::size_t>(frame[3]) << 8) |
                             std::to_integer<std::size_t>(frame[4]);
  parsed.value.assign(reinterpret_cast<const char*>(frame.data() + 5), length);
  return parsed;
}

TEST(GatewayRelayTransportTest, HelloFrameCarriesIdentityAndDeliveryMode) {
  const std::vector<std::byte> frame =
      MakeGatewayHelloFrame("gateway-a", event::DELIVERY_MODE_AT_LEAST_ONCE);

  ASSERT_GE(frame.size(), 5U);
  EXPECT_EQ(std::to_integer<uint8_t>(frame[0]), io::TlvFrame::kGatewayHello);
  const std::size_t length = (std::to_integer<std::size_t>(frame[1]) << 24) |
                             (std::to_integer<std::size_t>(frame[2]) << 16) |
                             (std::to_integer<std::size_t>(frame[3]) << 8) |
                             std::to_integer<std::size_t>(frame[4]);
  event::GatewayHello hello;
  ASSERT_TRUE(hello.ParseFromArray(reinterpret_cast<const char*>(frame.data() + 5),
                                   static_cast<int>(length)));
  EXPECT_EQ(hello.gateway_id(), "gateway-a");
  EXPECT_EQ(hello.delivery_mode(), event::DELIVERY_MODE_AT_LEAST_ONCE);
}

TEST(GatewayRelayTransportTest, ConfiguredGatewayIdIsUsed) {
  EXPECT_EQ(ResolveGatewayId("gateway-a"), "gateway-a");
}

TEST(GatewayRelayTransportTest, EmptyGatewayIdGeneratesAnIdentity) {
  const std::string generated = ResolveGatewayId("");

  EXPECT_FALSE(generated.empty());
  EXPECT_EQ(generated.rfind("gateway-", 0), 0U);
  EXPECT_GT(generated.size(), std::string("gateway-").size());
}

TEST(GatewayRelayTransportTest, GatewayWithoutEventPlaneSendsNoHandshake) {
  EXPECT_FALSE(BuildGatewayHello("", event::DELIVERY_MODE_BEST_EFFORT).has_value());

  const auto hello = BuildGatewayHello("gateway-a", event::DELIVERY_MODE_AT_LEAST_ONCE);
  ASSERT_TRUE(hello.has_value());
  ASSERT_GE(hello->size(), 5U);
  EXPECT_EQ(std::to_integer<uint8_t>((*hello)[0]), io::TlvFrame::kGatewayHello);
}

TEST(GatewayRelayTransportTest, EachRelayOperationSelectsANode) {
  CapturedMailbox first;
  CapturedMailbox second;
  const std::array<io::OutboundMailbox*, 2> mailboxes{first.mailbox.get(), second.mailbox.get()};
  std::size_t selections = 0;
  GatewayRelayTransport transport(
      [&]() -> io::OutboundMailbox* { return mailboxes[selections++ % mailboxes.size()]; });

  const std::string payload = "opaque";
  const std::array<std::string_view, 1> targets{"gateway-b"};
  ASSERT_TRUE(
      transport.Send(targets, std::as_bytes(std::span(payload.data(), payload.size()))).ok());
  ASSERT_TRUE(
      transport.Send(targets, std::as_bytes(std::span(payload.data(), payload.size()))).ok());

  EXPECT_EQ(selections, 2U);
  EXPECT_EQ(first.frames.size(), 1U);
  EXPECT_EQ(second.frames.size(), 1U);
}

TEST(GatewayRelayTransportTest, SendCarriesTargetsAndPayload) {
  CapturedMailbox target;
  GatewayRelayTransport transport([&]() -> io::OutboundMailbox* { return target.mailbox.get(); });

  const std::string payload = "opaque";
  const std::array<std::string_view, 2> targets{"gateway-b", "gateway-c"};
  ASSERT_TRUE(
      transport.Send(targets, std::as_bytes(std::span(payload.data(), payload.size()))).ok());

  ASSERT_EQ(target.frames.size(), 1U);
  const ParsedFrame frame = parseFrame(target.frames[0]);
  EXPECT_EQ(frame.type_id, io::TlvFrame::kPeerRelay);
  event::PeerRelay relay;
  ASSERT_TRUE(relay.ParseFromString(frame.value));
  ASSERT_EQ(relay.target_gateway_ids_size(), 2);
  EXPECT_EQ(relay.target_gateway_ids(0), "gateway-b");
  EXPECT_EQ(relay.target_gateway_ids(1), "gateway-c");
  EXPECT_EQ(relay.payload(), payload);
}

TEST(GatewayRelayTransportTest, BroadcastUsesEmptyTargetList) {
  CapturedMailbox target;
  GatewayRelayTransport transport([&]() -> io::OutboundMailbox* { return target.mailbox.get(); });

  const std::string payload = "broadcast";
  ASSERT_TRUE(transport.Broadcast(std::as_bytes(std::span(payload.data(), payload.size()))).ok());

  ASSERT_EQ(target.frames.size(), 1U);
  event::PeerRelay relay;
  ASSERT_TRUE(relay.ParseFromString(parseFrame(target.frames[0]).value));
  EXPECT_EQ(relay.target_gateway_ids_size(), 0);
  EXPECT_EQ(relay.payload(), payload);
}

TEST(GatewayRelayTransportTest, NoMailboxIsUnavailable) {
  GatewayRelayTransport transport([]() -> io::OutboundMailbox* { return nullptr; });
  const std::string payload = "opaque";
  const std::array<std::string_view, 1> targets{"gateway-b"};
  EXPECT_FALSE(
      transport.Send(targets, std::as_bytes(std::span(payload.data(), payload.size()))).ok());
  EXPECT_FALSE(transport.Broadcast(std::as_bytes(std::span(payload.data(), payload.size()))).ok());
}

TEST(GatewayRelayTransportTest, OversizedPayloadIsRejected) {
  CapturedMailbox target;
  GatewayRelayTransport transport([&]() -> io::OutboundMailbox* { return target.mailbox.get(); });
  const std::string payload(GatewayRelayTransport::kMaxPayloadSize + 1, 'x');
  EXPECT_FALSE(transport.Broadcast(std::as_bytes(std::span(payload.data(), payload.size()))).ok());
  EXPECT_TRUE(target.frames.empty());
}

TEST(GatewayRelayTransportTest, HandleFrameDeliversPayloadToReceiver) {
  CapturedMailbox target;
  GatewayRelayTransport transport([&]() -> io::OutboundMailbox* { return target.mailbox.get(); });
  CapturingReceiver receiver;
  transport.SetReceiver(&receiver);

  event::PeerRelayDelivery delivery;
  delivery.set_payload("relayed");
  std::string serialized;
  ASSERT_TRUE(delivery.SerializeToString(&serialized));
  const io::TlvFrame frame{.type_id = io::TlvFrame::kPeerRelayDelivery,
                           .value = std::as_bytes(std::span(serialized.data(), serialized.size()))};
  ASSERT_TRUE(transport.HandleFrame(frame).ok());
  EXPECT_EQ(receiver.payload_, "relayed");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::gateway
