#include <cstddef>
#include <span>
#include <string_view>

#include "test/mocks/event/mocks.hh"
#include "test/mocks/extensions/extensions_mocks.hh"

#include "absl/status/status.h"
#include "common/event/cloudevent.pb.h"
#include "common/loaders/event_plane_loader.hh"
#include "gateway/config/gateway.pb.h"
#include "gateway/extensions/event_planes/summary_mesh/summary_mesh.pb.h"
#include "gtest/gtest.h"
#include "strij/gateway/event_plane.hh"
#include "strij/gateway/gateway_transport.hh"

namespace strij::loaders {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

class FakeTransport final : public gateway::GatewayTransport {
public:
  auto Send(std::span<const std::string_view> /*peer_ids*/, std::span<const std::byte> /*payload*/)
      -> absl::Status override {
    return absl::OkStatus();
  }
  auto Broadcast(std::span<const std::byte> /*payload*/) -> absl::Status override {
    return absl::OkStatus();
  }
  void SetReceiver(gateway::GatewayTransportReceiver* receiver) override { receiver_ = receiver; }
  [[nodiscard]] auto MaxPayloadSize() const -> std::size_t override { return 1024 * 1024; }

private:
  gateway::GatewayTransportReceiver* receiver_{nullptr};
};

class FakeHost final : public gateway::EventPlaneHost {
public:
  void Deliver(std::string_view /*subscription_id*/, const event::CloudEvent& /*event*/,
               std::string_view /*token*/) override {}
};

TEST(EventPlaneLoaderTest, LoadsRegisteredSummaryMesh) {
  event::MockDispatcher dispatcher;
  FakeTransport transport;
  FakeHost host;
  extensions::MockGatewayFactoryContext context;
  gateway::EventPlaneDeps deps(dispatcher, "gateway-a", transport, host, nullptr);

  config::EventPlaneConfig config;
  config.mutable_extension()->set_name("summary_mesh");
  extensions::event_planes::summary_mesh::SummaryMeshConfig mesh_config;
  ASSERT_TRUE(config.mutable_extension()->mutable_typed_config()->PackFrom(mesh_config));

  auto plane = CreateEventPlane(config, context, deps);
  ASSERT_TRUE(plane.ok()) << plane.status().message();
  EXPECT_EQ((*plane)->SupportedDeliveryMode(), event::DELIVERY_MODE_BEST_EFFORT);
}

TEST(EventPlaneLoaderTest, UnknownExtensionFails) {
  event::MockDispatcher dispatcher;
  FakeTransport transport;
  FakeHost host;
  extensions::MockGatewayFactoryContext context;
  gateway::EventPlaneDeps deps(dispatcher, "gateway-a", transport, host, nullptr);

  config::EventPlaneConfig config;
  config.mutable_extension()->set_name("does_not_exist");
  EXPECT_FALSE(CreateEventPlane(config, context, deps).ok());
}

TEST(EventPlaneLoaderTest, BadTypedConfigFails) {
  event::MockDispatcher dispatcher;
  FakeTransport transport;
  FakeHost host;
  extensions::MockGatewayFactoryContext context;
  gateway::EventPlaneDeps deps(dispatcher, "gateway-a", transport, host, nullptr);

  config::EventPlaneConfig config;
  config.mutable_extension()->set_name("summary_mesh");
  config::EventPlaneConfig wrong_config;
  ASSERT_TRUE(config.mutable_extension()->mutable_typed_config()->PackFrom(wrong_config));
  EXPECT_FALSE(CreateEventPlane(config, context, deps).ok());
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::loaders
