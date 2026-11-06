#include <string>

#include "common/core/io/tlv_frame.hh"
#include "common/event/cloudevent.pb.h"
#include "common/event/event_plane.pb.h"
#include "gtest/gtest.h"

namespace strij::event {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(CloudEventTest, RequiredAndOptionalFieldsRoundTrip) {
  CloudEvent event;
  event.set_specversion("1.0");
  event.set_id("evt-1");
  event.set_source("tests");
  event.set_type("order.created");
  event.set_subject("orders/1");
  event.set_time("2026-01-01T00:00:00Z");
  event.set_datacontenttype("application/json");
  event.set_dataschema("https://example.test/schema");
  event.set_data("{\"id\":1}");
  (*event.mutable_attributes())["roomId"] = "room-1";

  std::string bytes;
  ASSERT_TRUE(event.SerializeToString(&bytes));

  CloudEvent parsed;
  ASSERT_TRUE(parsed.ParseFromString(bytes));
  EXPECT_EQ(parsed.specversion(), "1.0");
  EXPECT_EQ(parsed.id(), "evt-1");
  EXPECT_EQ(parsed.source(), "tests");
  EXPECT_EQ(parsed.type(), "order.created");
  EXPECT_EQ(parsed.subject(), "orders/1");
  EXPECT_EQ(parsed.time(), "2026-01-01T00:00:00Z");
  EXPECT_EQ(parsed.datacontenttype(), "application/json");
  EXPECT_EQ(parsed.dataschema(), "https://example.test/schema");
  EXPECT_EQ(parsed.data(), "{\"id\":1}");
  EXPECT_EQ(parsed.attributes().at("roomId"), "room-1");
}

TEST(EventPlaneFramePayloadsTest, GatewayHelloRoundTrip) {
  GatewayHello hello;
  hello.set_gateway_id("gateway-a");
  hello.set_delivery_mode(DELIVERY_MODE_AT_LEAST_ONCE);
  std::string bytes;
  ASSERT_TRUE(hello.SerializeToString(&bytes));
  GatewayHello parsed;
  ASSERT_TRUE(parsed.ParseFromString(bytes));
  EXPECT_EQ(parsed.gateway_id(), "gateway-a");
  EXPECT_EQ(parsed.delivery_mode(), DELIVERY_MODE_AT_LEAST_ONCE);
}

TEST(EventPlaneFramePayloadsTest, SubscriptionResultRoundTrip) {
  EventSubscriptionResult result;
  result.set_subscription_id("sub-1");
  result.set_accepted(false);
  result.set_reason("unsupported");
  result.set_delivery_mode(DELIVERY_MODE_BEST_EFFORT);
  std::string bytes;
  ASSERT_TRUE(result.SerializeToString(&bytes));
  EventSubscriptionResult parsed;
  ASSERT_TRUE(parsed.ParseFromString(bytes));
  EXPECT_EQ(parsed.subscription_id(), "sub-1");
  EXPECT_FALSE(parsed.accepted());
  EXPECT_EQ(parsed.reason(), "unsupported");
  EXPECT_EQ(parsed.delivery_mode(), DELIVERY_MODE_BEST_EFFORT);
}

TEST(EventPlaneFramePayloadsTest, AckRoundTrip) {
  EventAck ack;
  ack.set_subscription_id("sub-1");
  ack.set_token("token-1");
  std::string bytes;
  ASSERT_TRUE(ack.SerializeToString(&bytes));
  EventAck parsed;
  ASSERT_TRUE(parsed.ParseFromString(bytes));
  EXPECT_EQ(parsed.subscription_id(), "sub-1");
  EXPECT_EQ(parsed.token(), "token-1");
}

TEST(EventPlaneFramePayloadsTest, PeerRelayRoundTrip) {
  PeerRelay relay;
  relay.add_target_gateway_ids("gateway-b");
  relay.add_target_gateway_ids("gateway-c");
  relay.set_payload("opaque");
  std::string bytes;
  ASSERT_TRUE(relay.SerializeToString(&bytes));
  PeerRelay parsed;
  ASSERT_TRUE(parsed.ParseFromString(bytes));
  ASSERT_EQ(parsed.target_gateway_ids_size(), 2);
  EXPECT_EQ(parsed.target_gateway_ids(0), "gateway-b");
  EXPECT_EQ(parsed.target_gateway_ids(1), "gateway-c");
  EXPECT_EQ(parsed.payload(), "opaque");
}

TEST(EventPlaneFramePayloadsTest, SubscriptionRequestRoundTrip) {
  EventSubscriptionRequest request;
  request.set_subscription_id("sub-1");
  request.mutable_pattern()->set_type("order.created");
  request.set_delivery_mode(DELIVERY_MODE_AUTO);
  std::string bytes;
  ASSERT_TRUE(request.SerializeToString(&bytes));
  EventSubscriptionRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(bytes));
  EXPECT_EQ(parsed.subscription_id(), "sub-1");
  EXPECT_EQ(parsed.pattern().type(), "order.created");
  EXPECT_EQ(parsed.delivery_mode(), DELIVERY_MODE_AUTO);
}

TEST(TlvFrameConstantsTest, EventPlaneIdsAreAppendOnly) {
  EXPECT_EQ(io::TlvFrame::kGatewayHello, 11);
  EXPECT_EQ(io::TlvFrame::kPeerRelay, 12);
  EXPECT_EQ(io::TlvFrame::kPeerRelayDelivery, 13);
  EXPECT_EQ(io::TlvFrame::kEventPublish, 14);
  EXPECT_EQ(io::TlvFrame::kEventSubscribe, 15);
  EXPECT_EQ(io::TlvFrame::kEventUnsubscribe, 16);
  EXPECT_EQ(io::TlvFrame::kEventDelivery, 17);
  EXPECT_EQ(io::TlvFrame::kEventAck, 18);
  EXPECT_EQ(io::TlvFrame::kEventSubscribeResult, 19);
  EXPECT_EQ(io::TlvFrame::kTaskSubmission, 0);
  EXPECT_EQ(io::TlvFrame::kTaskDecline, 10);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::event
