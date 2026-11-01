#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "common/event/cloudevent.pb.h"
#include "common/event/event_plane.pb.h"
#include "gateway/extensions/event_planes/summary_mesh/summary_mesh_event_plane.hh"
#include "gtest/gtest.h"
#include "strij/extensions/event_plane.hh"
#include "strij/extensions/gateway_transport.hh"
#include "test/mocks/event/mocks.hh"

namespace strij::gateway::event_planes {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

class FakeHost final : public extensions::EventPlaneHost {
public:
  void Deliver(std::string_view subscription_id, const event::CloudEvent& event,
               std::string_view /*token*/) override {
    deliveries.emplace_back(std::string(subscription_id), event.id());
  }

  std::vector<std::pair<std::string, std::string>> deliveries;
};

class FakeTransport final : public extensions::GatewayTransport {
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

    if (peer != nullptr && peer->receiver != nullptr) {
      peer->receiver->OnMessage(payload);
    }
    return absl::OkStatus();
  }

  auto Broadcast(std::span<const std::byte> payload) -> absl::Status override {
    ++broadcast_count;
    if (peer != nullptr && peer->receiver != nullptr) {
      peer->receiver->OnMessage(payload);
    }
    return absl::OkStatus();
  }

  void SetReceiver(extensions::GatewayTransportReceiver* new_receiver) override {
    receiver = new_receiver;
  }

  [[nodiscard]] auto MaxPayloadSize() const -> std::size_t override { return 1024 * 1024; }

  FakeTransport* peer{nullptr};
  extensions::GatewayTransportReceiver* receiver{nullptr};
  std::vector<std::vector<std::string>> sent_targets;
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
    deps_a_ = std::make_unique<extensions::EventPlaneDeps>(*dispatcher_, "gateway-a", transport_a_,
                                                          host_a_, nullptr);
    deps_b_ = std::make_unique<extensions::EventPlaneDeps>(*dispatcher_, "gateway-b", transport_b_,
                                                          host_b_, nullptr);
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
  std::unique_ptr<extensions::EventPlaneDeps> deps_a_;
  std::unique_ptr<extensions::EventPlaneDeps> deps_b_;
  std::unique_ptr<SummaryMeshEventPlane> plane_a_;
  std::unique_ptr<SummaryMeshEventPlane> plane_b_;
};

TEST(SummaryMeshEventPlaneDeliveryModeTest, AutoResolvesBestEffort) {
  event::MockDispatcher dispatcher;
  FakeTransport transport;
  FakeHost host;
  extensions::EventPlaneDeps deps(dispatcher, "gateway-a", transport, host, nullptr);
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

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::gateway::event_planes
