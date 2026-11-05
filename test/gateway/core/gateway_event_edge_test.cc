#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/core/io/connection.hh"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/io/protocol_parser.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/event/cloudevent.pb.h"
#include "common/event/event_plane.pb.h"
#include "gateway/core/gateway_event_edge.hh"
#include "gtest/gtest.h"
#include "strij/extensions/event_plane.hh"

namespace strij::gateway {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

class FakeEventPlane final : public extensions::EventPlane {
public:
  void Publish(event::CloudEvent event) override { published.push_back(std::move(event)); }
  void AddSubscription(std::string subscription_id, event::EventPattern pattern) override {
    added.emplace_back(subscription_id, pattern.type());
  }
  void AttachSubscription(std::string_view subscription_id) override {
    attached.emplace_back(subscription_id);
  }
  void DetachSubscription(std::string_view subscription_id) override {
    detached.emplace_back(subscription_id);
  }
  void RemoveSubscription(std::string_view subscription_id) override {
    removed.emplace_back(subscription_id);
  }
  void Ack(std::string_view subscription_id, std::string_view token) override {
    acked.emplace_back(subscription_id, token);
  }
  [[nodiscard]] auto SupportedDeliveryMode() const -> event::DeliveryMode override {
    return supported;
  }
  auto ResolveDeliveryMode(event::DeliveryMode requested)
      -> absl::StatusOr<event::DeliveryMode> override {
    if (requested == event::DELIVERY_MODE_AUTO) {
      return supported;
    }
    if (requested == event::DELIVERY_MODE_AT_LEAST_ONCE &&
        supported != event::DELIVERY_MODE_AT_LEAST_ONCE) {
      return absl::UnimplementedError("best-effort only");
    }
    return requested;
  }
  void OnMessage(std::span<const std::byte> /*payload*/) override {}

  event::DeliveryMode supported{event::DELIVERY_MODE_BEST_EFFORT};
  std::vector<event::CloudEvent> published;
  std::vector<std::pair<std::string, std::string>> added;
  std::vector<std::string> attached;
  std::vector<std::string> detached;
  std::vector<std::string> removed;
  std::vector<std::pair<std::string, std::string>> acked;
};

class GatewayEventEdgeTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds_.data()));
    dispatcher_ = std::make_shared<event::MockDispatcher>();
    EXPECT_CALL(*dispatcher_,
                PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return());
    conn_ = std::make_unique<io::Connection>(fds_[0], dispatcher_, &owner_,
                                             [](io::Connection&) -> io::ProtocolParserPtr {
                                               return std::make_unique<io::TrivialParser>();
                                             });
    // Drain each queued write immediately so every Enqueue reaches PrepareWrite
    // and tests observe one frame per write.
    EXPECT_CALL(*dispatcher_,
                PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly([this](event::Completable* io, uint8_t /*tag*/, int /*fd*/,
                               std::span<const std::byte> buf, off_t /*offset*/) {
          writes_.emplace_back(buf.begin(), buf.end());
          io->HandleCompletion(1, static_cast<int>(buf.size()), 0);
        });
  }

  void TearDown() override {
    conn_.reset();
    close(fds_[1]);
  }

  static auto makeSubscribeFrame(std::string id) -> io::TlvFrame {
    static event::EventSubscriptionRequest request;
    request.set_subscription_id(id);
    request.mutable_pattern()->set_type("order.created");
    request.set_delivery_mode(event::DELIVERY_MODE_AUTO);
    static std::string serialized;
    request.SerializeToString(&serialized);
    return {.type_id = io::TlvFrame::kEventSubscribe,
            .value = std::as_bytes(std::span(serialized.data(), serialized.size()))};
  }

  std::array<int, 2> fds_{};
  std::shared_ptr<event::MockDispatcher> dispatcher_;
  event::DummyOwner owner_;
  io::ConnectionPtr conn_;
  std::vector<std::vector<std::byte>> writes_;
};

auto parseFrameValue(const std::vector<std::byte>& frame) -> std::string {
  EXPECT_GE(frame.size(), 5U);
  const std::size_t length = (std::to_integer<std::size_t>(frame[1]) << 24) |
                             (std::to_integer<std::size_t>(frame[2]) << 16) |
                             (std::to_integer<std::size_t>(frame[3]) << 8) |
                             std::to_integer<std::size_t>(frame[4]);
  return std::string(reinterpret_cast<const char*>(frame.data() + 5), length);
}

TEST_F(GatewayEventEdgeTest, SubscribeRegistersWithThePlane) {
  FakeEventPlane plane;
  GatewayEventEdge edge;
  edge.SetPlane(&plane);

  ASSERT_TRUE(edge.HandleSubscribeFrame(makeSubscribeFrame("sub-1"), conn_->Mailbox()).ok());
  EXPECT_EQ(edge.SubscriptionCount(), 1U);
  ASSERT_EQ(plane.added.size(), 1U);
  EXPECT_EQ(plane.added[0].first, "sub-1");
  EXPECT_EQ(plane.added[0].second, "order.created");
  ASSERT_EQ(plane.attached.size(), 1U);
  EXPECT_EQ(plane.attached[0], "sub-1");

  ASSERT_EQ(writes_.size(), 1U);
  EXPECT_EQ(std::to_integer<uint8_t>(writes_[0][0]), io::TlvFrame::kEventSubscribeResult);
  event::EventSubscriptionResult result;
  ASSERT_TRUE(result.ParseFromString(parseFrameValue(writes_[0])));
  EXPECT_TRUE(result.accepted());
  EXPECT_EQ(result.subscription_id(), "sub-1");
  EXPECT_EQ(result.delivery_mode(), event::DELIVERY_MODE_BEST_EFFORT);
}

TEST_F(GatewayEventEdgeTest, RejectsADuplicateActiveSubscription) {
  FakeEventPlane plane;
  GatewayEventEdge edge;
  edge.SetPlane(&plane);

  ASSERT_TRUE(edge.HandleSubscribeFrame(makeSubscribeFrame("sub-1"), conn_->Mailbox()).ok());
  EXPECT_FALSE(edge.HandleSubscribeFrame(makeSubscribeFrame("sub-1"), conn_->Mailbox()).ok());

  EXPECT_EQ(edge.SubscriptionCount(), 1U);
  ASSERT_EQ(plane.added.size(), 1U);
  ASSERT_EQ(writes_.size(), 2U);
  event::EventSubscriptionResult rejected;
  ASSERT_TRUE(rejected.ParseFromString(parseFrameValue(writes_[1])));
  EXPECT_FALSE(rejected.accepted());
  EXPECT_EQ(rejected.subscription_id(), "sub-1");

  writes_.clear();
  event::CloudEvent event;
  event.set_id("evt-1");
  edge.Deliver("sub-1", event, "token-1");
  EXPECT_EQ(writes_.size(), 1U);
}

TEST_F(GatewayEventEdgeTest, DeliverWritesAnEventDeliveryFrame) {
  FakeEventPlane plane;
  GatewayEventEdge edge;
  edge.SetPlane(&plane);
  ASSERT_TRUE(edge.HandleSubscribeFrame(makeSubscribeFrame("sub-1"), conn_->Mailbox()).ok());
  writes_.clear();

  event::CloudEvent event;
  event.set_specversion("1.0");
  event.set_id("evt-1");
  event.set_source("tests");
  event.set_type("order.created");

  edge.Deliver("sub-1", event, "token-1");

  ASSERT_EQ(writes_.size(), 1U);
  EXPECT_EQ(std::to_integer<uint8_t>(writes_[0][0]), io::TlvFrame::kEventDelivery);
  event::EventDelivery delivery;
  ASSERT_TRUE(delivery.ParseFromString(parseFrameValue(writes_[0])));
  EXPECT_EQ(delivery.subscription_id(), "sub-1");
  EXPECT_EQ(delivery.token(), "token-1");
  EXPECT_EQ(delivery.event().id(), "evt-1");
}

TEST_F(GatewayEventEdgeTest, UnsubscribeRemovesAndStopsDelivery) {
  FakeEventPlane plane;
  GatewayEventEdge edge;
  edge.SetPlane(&plane);
  ASSERT_TRUE(edge.HandleSubscribeFrame(makeSubscribeFrame("sub-1"), conn_->Mailbox()).ok());

  event::EventUnsubscribe unsubscribe;
  unsubscribe.set_subscription_id("sub-1");
  std::string serialized;
  ASSERT_TRUE(unsubscribe.SerializeToString(&serialized));
  const io::TlvFrame frame{.type_id = io::TlvFrame::kEventUnsubscribe,
                           .value = std::as_bytes(std::span(serialized.data(), serialized.size()))};
  ASSERT_TRUE(edge.HandleUnsubscribeFrame(frame).ok());
  EXPECT_EQ(edge.SubscriptionCount(), 0U);
  ASSERT_EQ(plane.removed.size(), 1U);
  EXPECT_EQ(plane.removed[0], "sub-1");

  writes_.clear();
  event::CloudEvent event;
  event.set_id("evt-1");
  edge.Deliver("sub-1", event, "token-1");
  EXPECT_TRUE(writes_.empty());
}

TEST_F(GatewayEventEdgeTest, UsesTheExtensionDeliveryModeAndForwardsAck) {
  FakeEventPlane plane;
  plane.supported = event::DELIVERY_MODE_AT_LEAST_ONCE;
  GatewayEventEdge edge;
  edge.SetPlane(&plane);

  ASSERT_TRUE(edge.HandleSubscribeFrame(makeSubscribeFrame("sub-1"), conn_->Mailbox()).ok());
  ASSERT_EQ(writes_.size(), 1U);
  event::EventSubscriptionResult result;
  ASSERT_TRUE(result.ParseFromString(parseFrameValue(writes_[0])));
  EXPECT_TRUE(result.accepted());
  EXPECT_EQ(result.delivery_mode(), event::DELIVERY_MODE_AT_LEAST_ONCE);

  event::EventAck ack;
  ack.set_subscription_id("sub-1");
  ack.set_token("token-1");
  std::string serialized;
  ASSERT_TRUE(ack.SerializeToString(&serialized));
  const io::TlvFrame frame{.type_id = io::TlvFrame::kEventAck,
                           .value = std::as_bytes(std::span(serialized.data(), serialized.size()))};
  ASSERT_TRUE(edge.HandleAckFrame(frame).ok());
  ASSERT_EQ(plane.acked.size(), 1U);
  EXPECT_EQ(plane.acked[0].first, "sub-1");
  EXPECT_EQ(plane.acked[0].second, "token-1");
}

TEST_F(GatewayEventEdgeTest, AckIsForwardedToThePlane) {
  FakeEventPlane plane;
  GatewayEventEdge edge;
  edge.SetPlane(&plane);

  event::EventAck ack;
  ack.set_subscription_id("sub-1");
  ack.set_token("token-1");
  std::string serialized;
  ASSERT_TRUE(ack.SerializeToString(&serialized));
  const io::TlvFrame frame{.type_id = io::TlvFrame::kEventAck,
                           .value = std::as_bytes(std::span(serialized.data(), serialized.size()))};
  ASSERT_TRUE(edge.HandleAckFrame(frame).ok());
  ASSERT_EQ(plane.acked.size(), 1U);
  EXPECT_EQ(plane.acked[0].first, "sub-1");
  EXPECT_EQ(plane.acked[0].second, "token-1");
}

TEST_F(GatewayEventEdgeTest, ConnectionCloseDetachesSubscriptions) {
  FakeEventPlane plane;
  GatewayEventEdge edge;
  edge.SetPlane(&plane);
  ASSERT_TRUE(edge.HandleSubscribeFrame(makeSubscribeFrame("sub-1"), conn_->Mailbox()).ok());

  conn_->Close();
  EXPECT_EQ(edge.SubscriptionCount(), 0U);
  ASSERT_EQ(plane.detached.size(), 1U);
  EXPECT_EQ(plane.detached[0], "sub-1");
}

TEST_F(GatewayEventEdgeTest, PublishIsForwardedToThePlane) {
  FakeEventPlane plane;
  GatewayEventEdge edge;
  edge.SetPlane(&plane);

  event::CloudEvent event;
  event.set_specversion("1.0");
  event.set_id("evt-1");
  event.set_source("tests");
  event.set_type("order.created");
  std::string serialized;
  ASSERT_TRUE(event.SerializeToString(&serialized));
  const io::TlvFrame frame{.type_id = io::TlvFrame::kEventPublish,
                           .value = std::as_bytes(std::span(serialized.data(), serialized.size()))};
  ASSERT_TRUE(edge.HandlePublishFrame(frame).ok());
  ASSERT_EQ(plane.published.size(), 1U);
  EXPECT_EQ(plane.published[0].id(), "evt-1");
}

TEST_F(GatewayEventEdgeTest, ReattachesTheSameStableSubscriptionId) {
  FakeEventPlane plane;
  plane.supported = event::DELIVERY_MODE_AT_LEAST_ONCE;
  GatewayEventEdge edge;
  edge.SetPlane(&plane);
  ASSERT_TRUE(edge.HandleSubscribeFrame(makeSubscribeFrame("sub-1"), conn_->Mailbox()).ok());
  ASSERT_EQ(plane.added.size(), 1U);

  conn_->Close();
  EXPECT_EQ(edge.SubscriptionCount(), 0U);
  ASSERT_EQ(plane.detached.size(), 1U);
  EXPECT_EQ(plane.detached[0], "sub-1");

  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  EXPECT_CALL(*dispatcher,
              PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(1);
  io::Connection conn2(fds[0], dispatcher, &owner, [](io::Connection&) -> io::ProtocolParserPtr {
    return std::make_unique<io::TrivialParser>();
  });

  ASSERT_TRUE(edge.HandleSubscribeFrame(makeSubscribeFrame("sub-1"), conn2.Mailbox()).ok());
  EXPECT_EQ(edge.SubscriptionCount(), 1U);
  ASSERT_EQ(plane.added.size(), 2U);
  EXPECT_EQ(plane.added[1].first, "sub-1");
  ASSERT_EQ(plane.attached.size(), 2U);
  EXPECT_EQ(plane.attached[1], "sub-1");
  close(fds[1]);
}

TEST_F(GatewayEventEdgeTest, NoEventPlaneRejectsEventOperations) {
  GatewayEventEdge edge;
  EXPECT_FALSE(
      edge.HandlePublishFrame(io::TlvFrame{.type_id = io::TlvFrame::kEventPublish, .value = {}})
          .ok());
  EXPECT_FALSE(
      edge.HandleSubscribeFrame(io::TlvFrame{.type_id = io::TlvFrame::kEventSubscribe, .value = {}},
                                conn_->Mailbox())
          .ok());
  EXPECT_FALSE(edge.HandleUnsubscribeFrame(
                       io::TlvFrame{.type_id = io::TlvFrame::kEventUnsubscribe, .value = {}})
                   .ok());
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::gateway
