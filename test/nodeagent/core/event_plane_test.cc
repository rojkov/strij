#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "absl/status/status.h"
#include "common/core/io/connection.hh"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/io/protocol_parser.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/event/cloudevent.pb.h"
#include "common/event/event_plane.pb.h"
#include "gtest/gtest.h"
#include "nodeagent/core/gateway_connection_pool.hh"
#include "nodeagent/core/node_event_client.hh"
#include "nodeagent/core/node_relay_service.hh"

namespace strij::nodeagent {
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

struct ParsedFrame {
  uint8_t type_id{0};
  std::string value;
};

auto parseFrame(const std::vector<std::byte>& frame) -> ParsedFrame {
  EXPECT_GE(frame.size(), 5U);
  ParsedFrame parsed;
  parsed.type_id = std::bit_cast<uint8_t>(frame[0]);
  const std::size_t length = (std::to_integer<std::size_t>(frame[1]) << 24) |
                             (std::to_integer<std::size_t>(frame[2]) << 16) |
                             (std::to_integer<std::size_t>(frame[3]) << 8) |
                             std::to_integer<std::size_t>(frame[4]);
  parsed.value.assign(reinterpret_cast<const char*>(frame.data() + 5), length);
  return parsed;
}

auto makeRelayFrame(const event::PeerRelay& relay) -> io::TlvFrame {
  static std::string storage;
  EXPECT_TRUE(relay.SerializeToString(&storage));
  return io::TlvFrame{.type_id = io::TlvFrame::kPeerRelay,
                      .value = std::as_bytes(std::span(storage.data(), storage.size()))};
}

auto makeHelloFrame(std::string gateway_id,
                    event::DeliveryMode mode = event::DELIVERY_MODE_BEST_EFFORT) -> io::TlvFrame {
  static std::string storage;
  event::GatewayHello hello;
  hello.set_gateway_id(std::move(gateway_id));
  hello.set_delivery_mode(mode);
  EXPECT_TRUE(hello.SerializeToString(&storage));
  return io::TlvFrame{.type_id = io::TlvFrame::kGatewayHello,
                      .value = std::as_bytes(std::span(storage.data(), storage.size()))};
}

auto makeSubscribeResultFrame(std::string subscription_id, bool accepted, event::DeliveryMode mode)
    -> io::TlvFrame {
  static std::string storage;
  event::EventSubscriptionResult result;
  result.set_subscription_id(std::move(subscription_id));
  result.set_accepted(accepted);
  result.set_delivery_mode(mode);
  EXPECT_TRUE(result.SerializeToString(&storage));
  return io::TlvFrame{.type_id = io::TlvFrame::kEventSubscribeResult,
                      .value = std::as_bytes(std::span(storage.data(), storage.size()))};
}

auto makeDeliveryFrame(const event::EventDelivery& delivery) -> io::TlvFrame {
  static std::string storage;
  EXPECT_TRUE(delivery.SerializeToString(&storage));
  return io::TlvFrame{.type_id = io::TlvFrame::kEventDelivery,
                      .value = std::as_bytes(std::span(storage.data(), storage.size()))};
}

TEST(GatewayConnectionPoolTest, BroadcastExcludesSourceAndTargetsResolve) {
  GatewayConnectionPool connections;
  CapturedMailbox first;
  CapturedMailbox second;
  connections.Register(first.mailbox);
  connections.Register(second.mailbox);

  EXPECT_EQ(connections.LiveCount(), 2U);
  connections.SetGatewayId(second.mailbox.get(), "gateway-b");
  EXPECT_EQ(connections.Find("gateway-b"), second.mailbox.get());

  const std::vector<std::byte> frame{std::byte{0xAB}};
  connections.Broadcast(frame, first.mailbox.get());

  EXPECT_TRUE(first.frames.empty());
  ASSERT_EQ(second.frames.size(), 1U);
  EXPECT_EQ(second.frames[0], frame);
}

TEST(GatewayConnectionPoolTest, ClosedConnectionIsPruned) {
  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());

  GatewayConnectionPool pool;
  {
    io::Connection conn(fds[0], dispatcher, &owner, [](io::Connection&) -> io::ProtocolParserPtr {
      return std::make_unique<io::TrivialParser>();
    });
    pool.Register(conn.Mailbox());
    EXPECT_EQ(pool.LiveCount(), 1U);
    conn.Close();
    EXPECT_EQ(pool.LiveCount(), 0U);
    EXPECT_EQ(pool.Next(), nullptr);
  }
  close(fds[1]);
}

TEST(NodeEventClientTest, PublishSubscribeAndDelivery) {
  GatewayConnectionPool connections;
  CapturedMailbox gateway;
  connections.Register(gateway.mailbox);
  connections.SetGatewayId(gateway.mailbox.get(), "gateway-a", event::DELIVERY_MODE_BEST_EFFORT);
  NodeEventClient client(connections);

  event::CloudEvent event;
  event.set_specversion("1.0");
  event.set_id("evt-1");
  event.set_source("tests");
  event.set_type("order.created");
  ASSERT_TRUE(client.Publish(event).ok());
  ASSERT_EQ(gateway.frames.size(), 1U);
  const ParsedFrame published = parseFrame(gateway.frames[0]);
  EXPECT_EQ(published.type_id, io::TlvFrame::kEventPublish);
  event::CloudEvent parsed_event;
  ASSERT_TRUE(parsed_event.ParseFromString(published.value));
  EXPECT_EQ(parsed_event.id(), "evt-1");

  absl::Status ready_status = absl::InternalError("not called");
  event::DeliveryMode ready_mode = event::DELIVERY_MODE_AUTO;
  std::vector<std::string> delivered;
  event::EventSubscriptionRequest request;
  request.set_subscription_id("sub-1");
  request.mutable_pattern()->set_type("order.created");
  request.set_delivery_mode(event::DELIVERY_MODE_AUTO);
  auto subscription = client.Subscribe(
      request,
      [&](absl::Status status, event::DeliveryMode mode) {
        ready_status = std::move(status);
        ready_mode = mode;
      },
      [&](const event::CloudEvent& delivered_event, std::string_view token) {
        delivered.push_back(delivered_event.id() + ":" + std::string(token));
      });
  ASSERT_TRUE(subscription != nullptr);
  EXPECT_FALSE(ready_status.ok()) << "ready must wait for the gateway edge";
  ASSERT_EQ(gateway.frames.size(), 2U);
  EXPECT_EQ(parseFrame(gateway.frames[1]).type_id, io::TlvFrame::kEventSubscribe);

  client.HandleSubscribeResult(
      makeSubscribeResultFrame("sub-1", true, event::DELIVERY_MODE_BEST_EFFORT));
  EXPECT_TRUE(ready_status.ok());
  EXPECT_EQ(ready_mode, event::DELIVERY_MODE_BEST_EFFORT);

  event::EventDelivery delivery;
  delivery.set_subscription_id("sub-1");
  delivery.set_token("token-1");
  *delivery.mutable_event() = event;
  client.HandleDelivery(makeDeliveryFrame(delivery));
  ASSERT_EQ(delivered.size(), 1U);
  EXPECT_EQ(delivered[0], "evt-1:token-1");
  subscription->Ack("token-1");
  EXPECT_EQ(gateway.frames.size(), 2U) << "best-effort ack is a no-op";

  subscription->Delete();
  ASSERT_EQ(gateway.frames.size(), 3U);
  EXPECT_EQ(parseFrame(gateway.frames[2]).type_id, io::TlvFrame::kEventUnsubscribe);
}

TEST(NodeEventClientTest, DuplicateActiveSubscriptionFailsReady) {
  GatewayConnectionPool connections;
  CapturedMailbox gateway;
  connections.Register(gateway.mailbox);
  connections.SetGatewayId(gateway.mailbox.get(), "gateway-a", event::DELIVERY_MODE_BEST_EFFORT);
  NodeEventClient client(connections);

  event::EventSubscriptionRequest request;
  request.set_subscription_id("sub-1");
  request.mutable_pattern()->set_type("order.created");
  request.set_delivery_mode(event::DELIVERY_MODE_AUTO);
  auto first = client.Subscribe(
      request, [](absl::Status, event::DeliveryMode) {},
      [](const event::CloudEvent&, std::string_view) {});
  ASSERT_NE(first, nullptr);
  ASSERT_EQ(gateway.frames.size(), 1U);

  absl::Status duplicate_status = absl::OkStatus();
  auto duplicate = client.Subscribe(
      request,
      [&](absl::Status status, event::DeliveryMode /*mode*/) {
        duplicate_status = std::move(status);
      },
      [](const event::CloudEvent&, std::string_view) {});

  EXPECT_EQ(duplicate_status.code(), absl::StatusCode::kAlreadyExists);
  EXPECT_NE(duplicate, nullptr);
  EXPECT_EQ(gateway.frames.size(), 1U) << "a duplicate must not write a second subscribe frame";
}

TEST(NodeEventClientTest, SubscribeWithoutGatewayFailsReady) {
  GatewayConnectionPool connections;
  NodeEventClient client(connections);
  event::EventSubscriptionRequest request;
  request.set_subscription_id("sub-1");
  request.set_delivery_mode(event::DELIVERY_MODE_AUTO);

  absl::Status ready_status = absl::OkStatus();
  auto subscription = client.Subscribe(
      request, [&](absl::Status status, event::DeliveryMode /*mode*/) { ready_status = status; },
      [](const event::CloudEvent& /*event*/, std::string_view /*token*/) {});
  EXPECT_EQ(ready_status.code(), absl::StatusCode::kUnavailable);
  EXPECT_NE(subscription, nullptr);
}

TEST(NodeEventClientTest, WithoutEventPlanePublishAndSubscribeFailUnimplemented) {
  GatewayConnectionPool connections;
  CapturedMailbox gateway;
  connections.Register(gateway.mailbox);
  NodeEventClient client(connections);

  event::CloudEvent event;
  event.set_id("evt-1");
  EXPECT_EQ(client.Publish(event).code(), absl::StatusCode::kUnimplemented);

  event::EventSubscriptionRequest request;
  request.set_subscription_id("sub-1");
  request.set_delivery_mode(event::DELIVERY_MODE_AUTO);
  absl::Status ready_status = absl::OkStatus();
  client.Subscribe(
      request, [&](absl::Status status, event::DeliveryMode /*mode*/) { ready_status = status; },
      [](const event::CloudEvent& /*event*/, std::string_view /*token*/) {});
  EXPECT_EQ(ready_status.code(), absl::StatusCode::kUnimplemented);
  EXPECT_TRUE(gateway.frames.empty());
}

TEST(NodeEventClientTest, AtLeastOnceSubscriptionAcksWithTheOpaqueToken) {
  GatewayConnectionPool connections;
  CapturedMailbox gateway;
  connections.Register(gateway.mailbox);
  connections.SetGatewayId(gateway.mailbox.get(), "gateway-a", event::DELIVERY_MODE_AT_LEAST_ONCE);
  NodeEventClient client(connections);

  event::EventSubscriptionRequest request;
  request.set_subscription_id("sub-1");
  request.set_delivery_mode(event::DELIVERY_MODE_AUTO);

  absl::Status ready_status = absl::InternalError("not called");
  event::DeliveryMode ready_mode = event::DELIVERY_MODE_AUTO;
  auto subscription = client.Subscribe(
      request,
      [&](absl::Status status, event::DeliveryMode mode) {
        ready_status = std::move(status);
        ready_mode = mode;
      },
      [](const event::CloudEvent& /*event*/, std::string_view /*token*/) {});
  client.HandleSubscribeResult(
      makeSubscribeResultFrame("sub-1", true, event::DELIVERY_MODE_AT_LEAST_ONCE));
  EXPECT_TRUE(ready_status.ok());
  EXPECT_EQ(ready_mode, event::DELIVERY_MODE_AT_LEAST_ONCE);

  const std::size_t before_ack = gateway.frames.size();
  subscription->Ack("token-1");
  ASSERT_EQ(gateway.frames.size(), before_ack + 1);
  const ParsedFrame ack_frame = parseFrame(gateway.frames.back());
  EXPECT_EQ(ack_frame.type_id, io::TlvFrame::kEventAck);
  event::EventAck ack;
  ASSERT_TRUE(ack.ParseFromString(ack_frame.value));
  EXPECT_EQ(ack.subscription_id(), "sub-1");
  EXPECT_EQ(ack.token(), "token-1");
}

TEST(NodeEventClientTest, ExplicitBestEffortOnAtLeastOnceGatewayDoesNotAck) {
  GatewayConnectionPool connections;
  CapturedMailbox gateway;
  connections.Register(gateway.mailbox);
  connections.SetGatewayId(gateway.mailbox.get(), "gateway-a", event::DELIVERY_MODE_AT_LEAST_ONCE);
  NodeEventClient client(connections);

  event::EventSubscriptionRequest request;
  request.set_subscription_id("sub-1");
  request.set_delivery_mode(event::DELIVERY_MODE_BEST_EFFORT);

  auto subscription = client.Subscribe(
      request, [](absl::Status /*status*/, event::DeliveryMode /*mode*/) {},
      [](const event::CloudEvent& /*event*/, std::string_view /*token*/) {});
  client.HandleSubscribeResult(
      makeSubscribeResultFrame("sub-1", true, event::DELIVERY_MODE_BEST_EFFORT));

  const std::size_t before_ack = gateway.frames.size();
  subscription->Ack("token-1");
  EXPECT_EQ(gateway.frames.size(), before_ack);
}

TEST(NodeEventClientTest, RejectedSubscriptionFailsReady) {
  GatewayConnectionPool connections;
  CapturedMailbox gateway;
  connections.Register(gateway.mailbox);
  connections.SetGatewayId(gateway.mailbox.get(), "gateway-a", event::DELIVERY_MODE_BEST_EFFORT);
  NodeEventClient client(connections);

  event::EventSubscriptionRequest request;
  request.set_subscription_id("sub-1");
  request.set_delivery_mode(event::DELIVERY_MODE_AUTO);
  absl::Status ready_status = absl::OkStatus();
  auto subscription = client.Subscribe(
      request,
      [&](absl::Status status, event::DeliveryMode /*mode*/) { ready_status = std::move(status); },
      [](const event::CloudEvent& /*event*/, std::string_view /*token*/) {});

  client.HandleSubscribeResult(
      makeSubscribeResultFrame("sub-1", false, event::DELIVERY_MODE_BEST_EFFORT));
  EXPECT_EQ(ready_status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(NodeEventClientTest, ConnectionLossFailsAReadySubscription) {
  GatewayConnectionPool connections;
  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  io::Connection conn(fds[0], dispatcher, &owner, [](io::Connection&) -> io::ProtocolParserPtr {
    return std::make_unique<io::TrivialParser>();
  });
  connections.Register(conn.Mailbox());
  connections.SetGatewayId(conn.Mailbox().get(), "gateway-a", event::DELIVERY_MODE_BEST_EFFORT);
  NodeEventClient client(connections);

  event::EventSubscriptionRequest request;
  request.set_subscription_id("sub-1");
  request.set_delivery_mode(event::DELIVERY_MODE_AUTO);
  absl::Status error_status = absl::OkStatus();
  auto subscription = client.Subscribe(
      request, [](absl::Status /*status*/, event::DeliveryMode /*mode*/) {},
      [](const event::CloudEvent& /*event*/, std::string_view /*token*/) {},
      [&](absl::Status status) { error_status = std::move(status); });
  client.HandleSubscribeResult(
      makeSubscribeResultFrame("sub-1", true, event::DELIVERY_MODE_BEST_EFFORT));
  conn.Close();

  EXPECT_EQ(error_status.code(), absl::StatusCode::kUnavailable);
  close(fds[1]);
}

TEST(NodeEventClientTest, ConnectionLossBeforeReadyReportsReadyError) {
  GatewayConnectionPool connections;
  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  io::Connection conn(fds[0], dispatcher, &owner, [](io::Connection&) -> io::ProtocolParserPtr {
    return std::make_unique<io::TrivialParser>();
  });
  connections.Register(conn.Mailbox());
  connections.SetGatewayId(conn.Mailbox().get(), "gateway-a", event::DELIVERY_MODE_BEST_EFFORT);
  NodeEventClient client(connections);

  event::EventSubscriptionRequest request;
  request.set_subscription_id("sub-1");
  request.set_delivery_mode(event::DELIVERY_MODE_AUTO);
  absl::Status ready_status = absl::OkStatus();
  auto subscription = client.Subscribe(
      request,
      [&](absl::Status status, event::DeliveryMode /*mode*/) { ready_status = std::move(status); },
      [](const event::CloudEvent& /*event*/, std::string_view /*token*/) {});
  conn.Close();

  EXPECT_EQ(ready_status.code(), absl::StatusCode::kUnavailable);
  close(fds[1]);
}

TEST(NodeRelayServiceTest, TargetedRelayReachesOnlyTarget) {
  GatewayConnectionPool connections;
  CapturedMailbox source;
  CapturedMailbox target;
  CapturedMailbox other;
  connections.Register(source.mailbox);
  connections.Register(target.mailbox);
  connections.Register(other.mailbox);
  connections.SetGatewayId(target.mailbox.get(), "gateway-b");
  connections.SetGatewayId(other.mailbox.get(), "gateway-c");
  NodeRelayService relay(connections);

  event::PeerRelay peer_relay;
  peer_relay.add_target_gateway_ids("gateway-b");
  peer_relay.set_payload("opaque");
  ASSERT_TRUE(relay.HandleFrame(makeRelayFrame(peer_relay), *source.mailbox).ok());

  EXPECT_TRUE(source.frames.empty());
  EXPECT_TRUE(other.frames.empty());
  ASSERT_EQ(target.frames.size(), 1U);
  const ParsedFrame forwarded = parseFrame(target.frames[0]);
  EXPECT_EQ(forwarded.type_id, io::TlvFrame::kPeerRelayDelivery);
  event::PeerRelayDelivery delivery;
  ASSERT_TRUE(delivery.ParseFromString(forwarded.value));
  EXPECT_EQ(delivery.payload(), "opaque");
}

TEST(NodeRelayServiceTest, HelloAssociatesGatewayIdentity) {
  GatewayConnectionPool connections;
  CapturedMailbox source;
  connections.Register(source.mailbox);
  NodeRelayService relay(connections);

  event::GatewayHello hello;
  hello.set_gateway_id("gateway-a");
  std::string serialized;
  ASSERT_TRUE(hello.SerializeToString(&serialized));
  const io::TlvFrame frame{.type_id = io::TlvFrame::kGatewayHello,
                           .value = std::as_bytes(std::span(serialized.data(), serialized.size()))};
  ASSERT_TRUE(relay.HandleFrame(frame, *source.mailbox).ok());
  EXPECT_EQ(connections.Find("gateway-a"), source.mailbox.get());
}

TEST(NodeRelayServiceTest, MissingTargetIsDropped) {
  GatewayConnectionPool connections;
  CapturedMailbox source;
  CapturedMailbox other;
  connections.Register(source.mailbox);
  connections.Register(other.mailbox);
  NodeRelayService relay(connections);

  event::PeerRelay peer_relay;
  peer_relay.add_target_gateway_ids("unknown");
  peer_relay.set_payload("opaque");
  ASSERT_TRUE(relay.HandleFrame(makeRelayFrame(peer_relay), *source.mailbox).ok());
  EXPECT_TRUE(source.frames.empty());
  EXPECT_TRUE(other.frames.empty());
}

TEST(NodeRelayServiceTest, OversizedPayloadIsRejected) {
  GatewayConnectionPool connections;
  CapturedMailbox source;
  CapturedMailbox target;
  connections.Register(source.mailbox);
  connections.Register(target.mailbox);
  connections.SetGatewayId(target.mailbox.get(), "gateway-b");
  NodeRelayService relay(connections);

  event::PeerRelay peer_relay;
  peer_relay.add_target_gateway_ids("gateway-b");
  peer_relay.set_payload(std::string(NodeRelayService::kMaxRelayPayload + 1, 'x'));
  EXPECT_FALSE(relay.HandleFrame(makeRelayFrame(peer_relay), *source.mailbox).ok());
  EXPECT_TRUE(target.frames.empty());
}

TEST(NodeRelayServiceTest, RepeatedHelloReplacesTheMapping) {
  GatewayConnectionPool connections;
  CapturedMailbox first;
  CapturedMailbox second;
  connections.Register(first.mailbox);
  connections.Register(second.mailbox);
  NodeRelayService relay(connections);

  event::GatewayHello hello;
  hello.set_gateway_id("gateway-a");
  std::string serialized;
  ASSERT_TRUE(hello.SerializeToString(&serialized));
  const io::TlvFrame frame{.type_id = io::TlvFrame::kGatewayHello,
                           .value = std::as_bytes(std::span(serialized.data(), serialized.size()))};
  ASSERT_TRUE(relay.HandleFrame(frame, *first.mailbox).ok());
  ASSERT_TRUE(relay.HandleFrame(frame, *second.mailbox).ok());
  EXPECT_EQ(connections.Find("gateway-a"), second.mailbox.get());

  event::PeerRelay peer_relay;
  peer_relay.add_target_gateway_ids("gateway-a");
  peer_relay.set_payload("targeted");
  ASSERT_TRUE(relay.HandleFrame(makeRelayFrame(peer_relay), *first.mailbox).ok());
  EXPECT_TRUE(first.frames.empty());
  EXPECT_EQ(second.frames.size(), 1U);
}

TEST(NodeRelayServiceTest, EmptyTargetsBroadcastExceptSource) {
  GatewayConnectionPool connections;
  CapturedMailbox source;
  CapturedMailbox first;
  CapturedMailbox second;
  connections.Register(source.mailbox);
  connections.Register(first.mailbox);
  connections.Register(second.mailbox);
  NodeRelayService relay(connections);

  event::PeerRelay peer_relay;
  peer_relay.set_payload("broadcast");
  ASSERT_TRUE(relay.HandleFrame(makeRelayFrame(peer_relay), *source.mailbox).ok());

  EXPECT_TRUE(source.frames.empty());
  EXPECT_EQ(first.frames.size(), 1U);
  EXPECT_EQ(second.frames.size(), 1U);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent
