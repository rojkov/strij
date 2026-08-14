#include <memory>
#include <string>

#include "core/config/nodeagent.pb.h"
#include "core/node/capabilities.pb.h"
#include "core/nodeagent/capabilities.hh"
#include "core/nodeagent/task_handler_manager.hh"
#include "core/task/task.pb.h"
#include "extensions/task_handlers/echo/echo_task_handler.hh"
#include "gtest/gtest.h"

namespace strij::nodeagent {
namespace {

class NodeagentCapabilitiesTest : public ::testing::Test {
protected:
  auto MakeManager() -> std::shared_ptr<TaskHandlerManager> {
    auto manager = std::make_shared<TaskHandlerManager>();
    manager->AddHandler("echo",
                        std::make_unique<strij::extensions::task_handlers::EchoTaskHandler>());
    return manager;
  }

  auto MakeConfig() -> strij::config::NodeAgentConfig {
    strij::config::NodeAgentConfig config;
    config.mutable_tlv_listener()->set_address("127.0.0.1");
    config.mutable_tlv_listener()->set_port(9090);
    auto* pool = config.add_pools();
    pool->set_name("cpu");
    pool->set_total(16);
    return config;
  }
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(NodeagentCapabilitiesTest, GenerateNodeIdIsStableWithinProcess) {
  const std::string first = GenerateNodeId();
  const std::string second = GenerateNodeId();
  EXPECT_EQ(first, first);
  EXPECT_NE(first, second);
  EXPECT_TRUE(first.starts_with("node-"));
}

TEST_F(NodeagentCapabilitiesTest, DerivesAdvertisementFromConfigAndManager) {
  auto config = MakeConfig();
  auto* reservation = config.add_reservations();
  reservation->set_task_type("echo");
  reservation->set_pool("cpu");
  reservation->set_amount(4);
  auto* handler_cap = config.add_handlers();
  handler_cap->set_task_type("echo");
  handler_cap->set_concurrency(1024);

  auto result = BuildNodeCapabilities(config, *MakeManager(), "node-abc");
  ASSERT_TRUE(result.ok());
  const auto& caps = result.value();

  EXPECT_EQ(caps.node_id(), "node-abc");
  EXPECT_EQ(caps.address(), "127.0.0.1:9090");
  EXPECT_EQ(caps.capability_version(), 1U);
  ASSERT_EQ(caps.pools_size(), 1);
  EXPECT_EQ(caps.pools(0).name(), "cpu");
  EXPECT_EQ(caps.pools(0).total(), 16U);
  ASSERT_EQ(caps.reservations_size(), 1);
  EXPECT_EQ(caps.reservations(0).task_type(), "echo");
  EXPECT_EQ(caps.reservations(0).pool(), "cpu");
  EXPECT_EQ(caps.reservations(0).amount(), 4U);
  ASSERT_EQ(caps.handlers_size(), 1);
  EXPECT_EQ(caps.handlers(0).task_type(), "echo");
  EXPECT_EQ(caps.handlers(0).concurrency(), 1024U);
  ASSERT_EQ(caps.update_channels_size(), 1);
  EXPECT_EQ(caps.update_channels(0).kind(), "heartbeat");
  ASSERT_EQ(caps.scheduling_protocols_size(), 1);
  EXPECT_EQ(caps.scheduling_protocols(0).name(), "push");
}

TEST_F(NodeagentCapabilitiesTest, RoundTripsThroughProtobuf) {
  auto config = MakeConfig();
  auto result = BuildNodeCapabilities(config, *MakeManager(), "node-xyz");
  ASSERT_TRUE(result.ok());

  std::string serialized;
  ASSERT_TRUE(result.value().SerializeToString(&serialized));

  strij::node::NodeCapabilities parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  EXPECT_EQ(parsed.node_id(), "node-xyz");
  EXPECT_EQ(parsed.address(), "127.0.0.1:9090");
  EXPECT_EQ(parsed.pools_size(), 1);
  EXPECT_EQ(parsed.pools(0).name(), "cpu");
  EXPECT_EQ(parsed.update_channels_size(), 1);
  EXPECT_EQ(parsed.scheduling_protocols_size(), 1);
}

TEST_F(NodeagentCapabilitiesTest, EmptyPoolsFailsStartup) {
  strij::config::NodeAgentConfig config;
  config.mutable_tlv_listener()->set_port(9090);
  auto result = BuildNodeCapabilities(config, *MakeManager(), "node-abc");
  ASSERT_FALSE(result.ok());
  EXPECT_TRUE(result.status().message().find("pools") != std::string::npos);
}

TEST_F(NodeagentCapabilitiesTest, ReservationWithUndeclaredPoolFails) {
  auto config = MakeConfig();
  auto* reservation = config.add_reservations();
  reservation->set_task_type("echo");
  reservation->set_pool("gpu.h100");
  reservation->set_amount(1);
  auto result = BuildNodeCapabilities(config, *MakeManager(), "node-abc");
  ASSERT_FALSE(result.ok());
  EXPECT_TRUE(result.status().message().find("gpu.h100") != std::string::npos);
}

TEST_F(NodeagentCapabilitiesTest, HandlerCapabilityWithoutRegisteredHandlerFails) {
  auto config = MakeConfig();
  config.add_handlers()->set_task_type("ghost");
  auto result = BuildNodeCapabilities(config, *MakeManager(), "node-abc");
  ASSERT_FALSE(result.ok());
  EXPECT_TRUE(result.status().message().find("ghost") != std::string::npos);
}

TEST_F(NodeagentCapabilitiesTest, HandlerCapabilityCarriesDefaultResources) {
  auto config = MakeConfig();
  auto* handler_cap = config.add_handlers();
  handler_cap->set_task_type("echo");
  (*handler_cap->mutable_default_resources()->mutable_resources())["cpu"] = 2;
  auto result = BuildNodeCapabilities(config, *MakeManager(), "node-abc");
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value().handlers_size(), 1);
  EXPECT_EQ(result.value().handlers(0).default_resources().resources().at("cpu"), 2U);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent
