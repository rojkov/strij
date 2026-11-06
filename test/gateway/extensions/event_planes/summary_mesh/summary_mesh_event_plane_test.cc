#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "test/mocks/event/mocks.hh"

#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/event/cloudevent.pb.h"
#include "common/event/event_plane.pb.h"
#include "gateway/core/gateway_event_edge.hh"
#include "gateway/extensions/event_planes/summary_mesh/summary_mesh_event_plane.hh"
#include "gtest/gtest.h"
#include "nodeagent/core/gateway_connection_pool.hh"
#include "nodeagent/core/node_event_client.hh"
#include "nodeagent/core/node_relay_service.hh"
#include "strij/gateway/event_plane.hh"
#include "strij/gateway/gateway_transport.hh"

namespace strij::gateway::event_planes {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

class FakeHost final : public EventPlaneHost {
public:
  void Deliver(std::string_view subscription_id, const event::CloudEvent& event,
               std::string_view /*token*/) override {
    deliveries.emplace_back(std::string(subscription_id), event.id());
  }

  std::vector<std::pair<std::string, std::string>> deliveries;
};

class FakeTransport final : public GatewayTransport {
public:
  auto Send(std::span<const std::string_view> peer_ids, std::span<const std::byte> payload)
      -> absl::Status override {
    std::vector<std::string> ids;
    ids.reserve(peer_ids.size());
    for (const std::string_view id : peer_ids) {
      ids.emplace_back(id);
    }
    sent_targets.push_back(std::move(ids));
    ++send_count;

    if (!send_status.ok()) {
      return send_status;
    }

    if (peer != nullptr && peer->receiver != nullptr) {
      peer->receiver->OnMessage(payload);
    }
    return absl::OkStatus();
  }

  auto Broadcast(std::span<const std::byte> payload) -> absl::Status override {
    ++broadcast_count;

    if (!broadcast_status.ok()) {
      return broadcast_status;
    }

    if (peer != nullptr && peer->receiver != nullptr) {
      peer->receiver->OnMessage(payload);
    }
    return absl::OkStatus();
  }

  void SetReceiver(GatewayTransportReceiver* new_receiver) override { receiver = new_receiver; }

  [[nodiscard]] auto MaxPayloadSize() const -> std::size_t override { return 1024 * 1024; }

  FakeTransport* peer{nullptr};
  GatewayTransportReceiver* receiver{nullptr};
  std::vector<std::vector<std::string>> sent_targets;
  absl::Status send_status{absl::OkStatus()};
  absl::Status broadcast_status{absl::OkStatus()};
  int send_count{0};
  int broadcast_count{0};
};

auto makeEvent(std::string type) -> event::CloudEvent {
  event::CloudEvent event;
  event.set_specversion("1.0");
  event.set_id("evt-1");
  event.set_source("tests");
  event.set_type(std::move(type));
  return event;
}

auto makePattern(std::string type) -> event::EventPattern {
  event::EventPattern pattern;
  pattern.set_type(std::move(type));
  return pattern;
}

class SummaryMeshEventPlaneTest : public ::testing::Test {
protected:
  void SetUp() override {
    dispatcher_ = std::make_shared<event::MockDispatcher>();
    deps_a_ =
        std::make_unique<EventPlaneDeps>(*dispatcher_, "gateway-a", transport_a_, host_a_, nullptr);
    deps_b_ =
        std::make_unique<EventPlaneDeps>(*dispatcher_, "gateway-b", transport_b_, host_b_, nullptr);
    plane_a_ = std::make_unique<SummaryMeshEventPlane>(*deps_a_, 1000, 5000);
    plane_b_ = std::make_unique<SummaryMeshEventPlane>(*deps_b_, 1000, 5000);
    transport_a_.peer = &transport_b_;
    transport_b_.peer = &transport_a_;
  }

  std::shared_ptr<event::MockDispatcher> dispatcher_;
  FakeTransport transport_a_;
  FakeTransport transport_b_;
  FakeHost host_a_;
  FakeHost host_b_;
  std::unique_ptr<EventPlaneDeps> deps_a_;
  std::unique_ptr<EventPlaneDeps> deps_b_;
  std::unique_ptr<SummaryMeshEventPlane> plane_a_;
  std::unique_ptr<SummaryMeshEventPlane> plane_b_;
};

TEST(SummaryMeshEventPlaneDeliveryModeTest, AutoResolvesBestEffort) {
  event::MockDispatcher dispatcher;
  FakeTransport transport;
  FakeHost host;
  EventPlaneDeps deps(dispatcher, "gateway-a", transport, host, nullptr);
  SummaryMeshEventPlane plane(deps, 1000, 5000);

  EXPECT_EQ(plane.SupportedDeliveryMode(), event::DELIVERY_MODE_BEST_EFFORT);
  const auto resolved = plane.ResolveDeliveryMode(event::DELIVERY_MODE_AUTO);
  ASSERT_TRUE(resolved.ok());
  EXPECT_EQ(*resolved, event::DELIVERY_MODE_BEST_EFFORT);
  EXPECT_FALSE(plane.ResolveDeliveryMode(event::DELIVERY_MODE_AT_LEAST_ONCE).ok());
}

TEST_F(SummaryMeshEventPlaneTest, DeliversLocallyAndToMatchingPeer) {
  plane_a_->AddSubscription("a", makePattern("order.created"));
  plane_b_->AddSubscription("b", makePattern("order.created"));

  plane_a_->Publish(makeEvent("order.created"));

  ASSERT_EQ(host_a_.deliveries.size(), 1U);
  EXPECT_EQ(host_a_.deliveries[0].first, "a");
  ASSERT_EQ(host_b_.deliveries.size(), 1U);
  EXPECT_EQ(host_b_.deliveries[0].first, "b");
}

TEST_F(SummaryMeshEventPlaneTest, UnrelatedPeerIsSkipped) {
  plane_a_->AddSubscription("a", makePattern("order.created"));
  plane_b_->AddSubscription("b", makePattern("order.paid"));

  plane_a_->Publish(makeEvent("order.created"));

  EXPECT_EQ(host_a_.deliveries.size(), 1U);
  EXPECT_TRUE(host_b_.deliveries.empty());
  EXPECT_TRUE(transport_a_.sent_targets.empty());
}

TEST_F(SummaryMeshEventPlaneTest, PeerFailureDoesNotStopLocalDelivery) {
  plane_a_->AddSubscription("a", makePattern("order.created"));
  plane_b_->AddSubscription("b", makePattern("order.created"));
  transport_a_.send_status = absl::UnavailableError("peer unreachable");

  plane_a_->Publish(makeEvent("order.created"));

  ASSERT_EQ(host_a_.deliveries.size(), 1U);
  EXPECT_EQ(host_a_.deliveries[0].first, "a");
  EXPECT_TRUE(host_b_.deliveries.empty());
  EXPECT_EQ(transport_a_.send_count, 1);
}

TEST_F(SummaryMeshEventPlaneTest, PartitionStillMakesLocalProgress) {
  plane_a_->AddSubscription("a", makePattern("order.created"));
  plane_b_->AddSubscription("b", makePattern("order.created"));
  transport_a_.send_status = absl::UnavailableError("partitioned");
  transport_b_.send_status = absl::UnavailableError("partitioned");

  plane_a_->Publish(makeEvent("order.created"));
  plane_b_->Publish(makeEvent("order.created"));

  ASSERT_EQ(host_a_.deliveries.size(), 1U);
  EXPECT_EQ(host_a_.deliveries[0].first, "a");
  ASSERT_EQ(host_b_.deliveries.size(), 1U);
  EXPECT_EQ(host_b_.deliveries[0].first, "b");
}

TEST_F(SummaryMeshEventPlaneTest, MatchAllSubscriptionReceivesEveryType) {
  plane_a_->AddSubscription("a", makePattern("order.created"));
  plane_b_->AddSubscription("b", event::EventPattern{});

  plane_a_->Publish(makeEvent("anything.at.all"));

  EXPECT_TRUE(host_a_.deliveries.empty());
  ASSERT_EQ(host_b_.deliveries.size(), 1U);
  EXPECT_EQ(host_b_.deliveries[0].first, "b");
}

TEST_F(SummaryMeshEventPlaneTest, RemoveSubscriptionStopsPeerDelivery) {
  plane_a_->AddSubscription("a", makePattern("order.created"));
  plane_b_->AddSubscription("b", makePattern("order.created"));
  plane_b_->RemoveSubscription("b");

  plane_a_->Publish(makeEvent("order.created"));

  EXPECT_EQ(host_a_.deliveries.size(), 1U);
  EXPECT_TRUE(host_b_.deliveries.empty());
}

TEST_F(SummaryMeshEventPlaneTest, StaleEpochIsIgnored) {
  event::EventPlaneMessage newer;
  newer.set_sender_id("gateway-b");
  newer.set_epoch(2);
  newer.mutable_summary()->add_types("order.paid");
  std::string newer_bytes;
  ASSERT_TRUE(newer.SerializeToString(&newer_bytes));
  plane_a_->OnMessage(std::as_bytes(std::span(newer_bytes.data(), newer_bytes.size())));

  event::EventPlaneMessage older;
  older.set_sender_id("gateway-b");
  older.set_epoch(1);
  older.mutable_summary()->add_types("order.created");
  std::string older_bytes;
  ASSERT_TRUE(older.SerializeToString(&older_bytes));
  plane_a_->OnMessage(std::as_bytes(std::span(older_bytes.data(), older_bytes.size())));

  plane_a_->Publish(makeEvent("order.created"));
  EXPECT_TRUE(transport_a_.sent_targets.empty());

  plane_a_->Publish(makeEvent("order.paid"));
  ASSERT_EQ(transport_a_.sent_targets.size(), 1U);
  ASSERT_EQ(transport_a_.sent_targets[0].size(), 1U);
  EXPECT_EQ(transport_a_.sent_targets[0][0], "gateway-b");
}

TEST_F(SummaryMeshEventPlaneTest, ExpiredPeerSummaryIsNotUsed) {
  event::EventPlaneMessage summary;
  summary.set_sender_id("gateway-b");
  summary.set_epoch(1);
  summary.mutable_summary()->add_types("order.created");
  std::string bytes;
  ASSERT_TRUE(summary.SerializeToString(&bytes));
  plane_a_->OnMessage(std::as_bytes(std::span(bytes.data(), bytes.size())));

  plane_a_->ExpirePeers(absl::Now() + absl::Seconds(10));
  plane_a_->Publish(makeEvent("order.created"));
  EXPECT_TRUE(transport_a_.sent_targets.empty());
}

TEST_F(SummaryMeshEventPlaneTest, LateSubscriptionMissesAnEarlierEvent) {
  plane_a_->AddSubscription("a", makePattern("order.created"));

  plane_a_->Publish(makeEvent("order.created"));
  EXPECT_TRUE(host_b_.deliveries.empty());

  plane_b_->AddSubscription("b", makePattern("order.created"));
  plane_a_->Publish(makeEvent("order.created"));

  ASSERT_EQ(host_b_.deliveries.size(), 1U);
  EXPECT_EQ(host_b_.deliveries[0].first, "b");
}

class RelayTransport final : public GatewayTransport {
public:
  RelayTransport(nodeagent::NodeRelayService& relay, io::OutboundMailbox& source)
      : relay_{relay}, source_{source} {}

  auto Send(std::span<const std::string_view> peer_ids, std::span<const std::byte> payload)
      -> absl::Status override {
    event::PeerRelay relay;
    for (const std::string_view peer_id : peer_ids) {
      relay.add_target_gateway_ids(std::string(peer_id));
    }
    relay.set_payload(std::string(reinterpret_cast<const char*>(payload.data()), payload.size()));
    std::string serialized;
    if (!relay.SerializeToString(&serialized)) {
      return absl::InternalError("failed to serialize relay");
    }
    const io::TlvFrame frame{.type_id = io::TlvFrame::kPeerRelay,
                             .value =
                                 std::as_bytes(std::span(serialized.data(), serialized.size()))};
    return relay_.HandleFrame(frame, source_);
  }

  auto Broadcast(std::span<const std::byte> payload) -> absl::Status override {
    return Send({}, payload);
  }

  void SetReceiver(GatewayTransportReceiver* new_receiver) override { receiver = new_receiver; }

  [[nodiscard]] auto MaxPayloadSize() const -> std::size_t override { return 1024 * 1024; }

  GatewayTransportReceiver* receiver{nullptr};

private:
  nodeagent::NodeRelayService& relay_;
  io::OutboundMailbox& source_;
};

struct GatewayMailbox {
  std::function<void(std::vector<std::byte>)> on_frame;
  std::shared_ptr<io::OutboundMailbox> mailbox;

  GatewayMailbox() {
    mailbox = std::make_shared<io::OutboundMailbox>(
        [this](std::vector<std::byte> frame) { on_frame(std::move(frame)); });
  }
};

auto peerRelayPayload(const std::vector<std::byte>& frame) -> std::string {
  const std::size_t length = (std::to_integer<std::size_t>(frame[1]) << 24) |
                             (std::to_integer<std::size_t>(frame[2]) << 16) |
                             (std::to_integer<std::size_t>(frame[3]) << 8) |
                             std::to_integer<std::size_t>(frame[4]);
  event::PeerRelayDelivery delivery;
  delivery.ParseFromArray(reinterpret_cast<const char*>(frame.data() + 5),
                          static_cast<int>(length));
  return delivery.payload();
}

TEST(SummaryMeshRelayIntegrationTest, DeliversAcrossTwoGatewaysThroughTheRelayNode) {
  nodeagent::GatewayConnectionPool relay_pool;
  GatewayMailbox gateway_a_mailbox;
  GatewayMailbox gateway_b_mailbox;
  relay_pool.Register(gateway_a_mailbox.mailbox);
  relay_pool.Register(gateway_b_mailbox.mailbox);
  nodeagent::NodeRelayService relay(relay_pool);

  const auto announce = [&relay](GatewayMailbox& gateway, std::string gateway_id) {
    event::GatewayHello hello;
    hello.set_gateway_id(std::move(gateway_id));
    hello.set_delivery_mode(event::DELIVERY_MODE_BEST_EFFORT);
    std::string serialized;
    ASSERT_TRUE(hello.SerializeToString(&serialized));
    const io::TlvFrame frame{.type_id = io::TlvFrame::kGatewayHello,
                             .value =
                                 std::as_bytes(std::span(serialized.data(), serialized.size()))};
    ASSERT_TRUE(relay.HandleFrame(frame, *gateway.mailbox).ok());
  };
  announce(gateway_a_mailbox, "gateway-a");
  announce(gateway_b_mailbox, "gateway-b");

  event::MockDispatcher dispatcher;
  FakeHost host_a;
  FakeHost host_b;
  RelayTransport transport_a(relay, *gateway_a_mailbox.mailbox);
  RelayTransport transport_b(relay, *gateway_b_mailbox.mailbox);
  EventPlaneDeps deps_a(dispatcher, "gateway-a", transport_a, host_a, nullptr);
  EventPlaneDeps deps_b(dispatcher, "gateway-b", transport_b, host_b, nullptr);
  SummaryMeshEventPlane plane_a(deps_a, 1000, 5000);
  SummaryMeshEventPlane plane_b(deps_b, 1000, 5000);

  gateway_a_mailbox.on_frame = [&](const std::vector<std::byte>& frame) {
    const std::string payload = peerRelayPayload(frame);
    plane_a.OnMessage(std::as_bytes(std::span(payload.data(), payload.size())));
  };
  gateway_b_mailbox.on_frame = [&](const std::vector<std::byte>& frame) {
    const std::string payload = peerRelayPayload(frame);
    plane_b.OnMessage(std::as_bytes(std::span(payload.data(), payload.size())));
  };

  plane_a.AddSubscription("a", makePattern("order.created"));
  plane_b.AddSubscription("b", makePattern("order.created"));
  plane_a.Publish(makeEvent("order.created"));

  ASSERT_EQ(host_a.deliveries.size(), 1U);
  ASSERT_EQ(host_b.deliveries.size(), 1U);
  EXPECT_EQ(host_b.deliveries[0].first, "b");
}

TEST(SummaryMeshNodeRelayIntegrationTest, EmitAndListenAcrossTwoGatewaysAndNodes) {
  nodeagent::GatewayConnectionPool relay_pool;
  GatewayMailbox gateway_a;
  GatewayMailbox gateway_b;
  relay_pool.Register(gateway_a.mailbox);
  relay_pool.Register(gateway_b.mailbox);
  nodeagent::NodeRelayService relay(relay_pool);

  nodeagent::GatewayConnectionPool node_a_connections;
  nodeagent::GatewayConnectionPool node_b_connections;
  node_a_connections.Register(gateway_a.mailbox);
  node_b_connections.Register(gateway_b.mailbox);

  const auto announce = [&](GatewayMailbox& gateway, std::string gateway_id) {
    event::GatewayHello hello;
    hello.set_gateway_id(gateway_id);
    hello.set_delivery_mode(event::DELIVERY_MODE_BEST_EFFORT);
    std::string serialized;
    ASSERT_TRUE(hello.SerializeToString(&serialized));
    const io::TlvFrame frame{.type_id = io::TlvFrame::kGatewayHello,
                             .value =
                                 std::as_bytes(std::span(serialized.data(), serialized.size()))};
    ASSERT_TRUE(relay.HandleFrame(frame, *gateway.mailbox).ok());
  };
  announce(gateway_a, "gateway-a");
  announce(gateway_b, "gateway-b");
  node_a_connections.SetGatewayId(gateway_a.mailbox.get(), "gateway-a",
                                  event::DELIVERY_MODE_BEST_EFFORT);
  node_b_connections.SetGatewayId(gateway_b.mailbox.get(), "gateway-b",
                                  event::DELIVERY_MODE_BEST_EFFORT);

  event::MockDispatcher dispatcher_a;
  event::MockDispatcher dispatcher_b;
  gateway::GatewayEventEdge edge_a;
  gateway::GatewayEventEdge edge_b;
  RelayTransport transport_a(relay, *gateway_a.mailbox);
  RelayTransport transport_b(relay, *gateway_b.mailbox);
  EventPlaneDeps deps_a(dispatcher_a, "gateway-a", transport_a, edge_a, nullptr);
  EventPlaneDeps deps_b(dispatcher_b, "gateway-b", transport_b, edge_b, nullptr);
  SummaryMeshEventPlane plane_a(deps_a, 1000, 5000);
  SummaryMeshEventPlane plane_b(deps_b, 1000, 5000);
  edge_a.SetPlane(&plane_a);
  edge_b.SetPlane(&plane_b);

  nodeagent::NodeEventClient node_a(node_a_connections);
  nodeagent::NodeEventClient node_b(node_b_connections);

  const auto route = [&](GatewayMailbox& self, gateway::GatewayEventEdge& edge,
                         RelayTransport& transport, nodeagent::NodeEventClient& node,
                         std::vector<std::byte> frame) {
    ASSERT_GE(frame.size(), 5U);
    const std::size_t length = (std::to_integer<std::size_t>(frame[1]) << 24) |
                               (std::to_integer<std::size_t>(frame[2]) << 16) |
                               (std::to_integer<std::size_t>(frame[3]) << 8) |
                               std::to_integer<std::size_t>(frame[4]);
    const io::TlvFrame tlv{.type_id = std::to_integer<std::uint8_t>(frame[0]),
                           .value = std::as_bytes(std::span(frame.data() + 5, length))};

    switch (tlv.type_id) {
    case io::TlvFrame::kPeerRelay:
      EXPECT_TRUE(relay.HandleFrame(tlv, *self.mailbox).ok());
      break;
    case io::TlvFrame::kPeerRelayDelivery: {
      event::PeerRelayDelivery delivery;
      ASSERT_TRUE(delivery.ParseFromArray(reinterpret_cast<const char*>(tlv.value.data()),
                                          static_cast<int>(tlv.value.size())));
      edge.Plane()->OnMessage(
          std::as_bytes(std::span(delivery.payload().data(), delivery.payload().size())));
      break;
    }
    case io::TlvFrame::kEventPublish:
      EXPECT_TRUE(edge.HandlePublishFrame(tlv).ok());
      break;
    case io::TlvFrame::kEventSubscribe:
      EXPECT_TRUE(edge.HandleSubscribeFrame(tlv, self.mailbox).ok());
      break;
    case io::TlvFrame::kEventUnsubscribe:
      EXPECT_TRUE(edge.HandleUnsubscribeFrame(tlv).ok());
      break;
    case io::TlvFrame::kEventAck:
      EXPECT_TRUE(edge.HandleAckFrame(tlv).ok());
      break;
    case io::TlvFrame::kEventDelivery:
      node.HandleDelivery(tlv);
      break;
    case io::TlvFrame::kEventSubscribeResult:
      node.HandleSubscribeResult(tlv);
      break;
    default:
      break;
    }
  };
  gateway_a.on_frame = [&](std::vector<std::byte> frame) {
    route(gateway_a, edge_a, transport_a, node_a, std::move(frame));
  };
  gateway_b.on_frame = [&](std::vector<std::byte> frame) {
    route(gateway_b, edge_b, transport_b, node_b, std::move(frame));
  };

  event::EventSubscriptionRequest request;
  request.set_subscription_id("sub-1");
  request.mutable_pattern()->set_type("order.created");
  request.set_delivery_mode(event::DELIVERY_MODE_AUTO);
  absl::Status ready_status = absl::InternalError("not called");
  std::vector<event::CloudEvent> delivered;
  auto subscription = node_b.Subscribe(
      request,
      [&](absl::Status status, event::DeliveryMode /*mode*/) { ready_status = std::move(status); },
      [&](const event::CloudEvent& event, std::string_view /*token*/) {
        delivered.push_back(event);
      });
  ASSERT_TRUE(ready_status.ok()) << ready_status.message();

  event::CloudEvent event;
  event.set_specversion("1.0");
  event.set_id("evt-1");
  event.set_source("tests");
  event.set_type("order.created");
  event.set_subject("orders/1");
  event.set_data(R"({"id":7})");
  ASSERT_TRUE(node_a.Publish(event).ok());

  ASSERT_EQ(delivered.size(), 1U);
  EXPECT_EQ(delivered[0].id(), "evt-1");
  EXPECT_EQ(delivered[0].source(), "tests");
  EXPECT_EQ(delivered[0].type(), "order.created");
  EXPECT_EQ(delivered[0].subject(), "orders/1");
  EXPECT_EQ(delivered[0].data(), R"({"id":7})");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::gateway::event_planes
