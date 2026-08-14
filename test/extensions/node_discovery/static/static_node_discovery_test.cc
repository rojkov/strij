#include <memory>
#include <string>
#include <vector>

#include "test/mocks/extensions/extensions_mocks.hh"

#include "extensions/node_discovery/static/static_node_discovery.pb.h"
#include "extensions/node_discovery/node_discovery.hh"
#include "extensions/node_discovery/static/static_node_discovery.hh"
#include "gmock/gmock.h"
#include "google/protobuf/message.h"
#include "gtest/gtest.h"

namespace strij::extensions::node_discovery {
namespace {

using ::testing::_;

class StaticNodeDiscoveryTest : public ::testing::Test {
protected:
  auto CollectNodes(std::unique_ptr<NodeDiscovery> discovery)
      -> std::vector<strij::extensions::NodeInfo> {
    std::vector<strij::extensions::NodeInfo> collected;
    discovery->Start([&collected](std::vector<strij::extensions::NodeInfo> nodes) {
      collected = std::move(nodes);
    });
    return collected;
  }
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(StaticNodeDiscoveryTest, NodeIdIsDerivedFromAddress) {
  auto discovery = std::make_unique<StaticNodeDiscovery>(
      std::vector<std::string>{"10.0.0.1:9090"});

  auto nodes = CollectNodes(std::move(discovery));

  ASSERT_EQ(nodes.size(), 1U);
  EXPECT_EQ(nodes[0].node_id, "10.0.0.1:9090");
  EXPECT_EQ(nodes[0].address, "10.0.0.1:9090");
}

TEST_F(StaticNodeDiscoveryTest, DeliversAllConfiguredAddresses) {
  auto discovery = std::make_unique<StaticNodeDiscovery>(
      std::vector<std::string>{"a:9090", "b:9090", "c:9090"});

  auto nodes = CollectNodes(std::move(discovery));

  ASSERT_EQ(nodes.size(), 3U);
  EXPECT_EQ(nodes[0].node_id, "a:9090");
  EXPECT_EQ(nodes[1].node_id, "b:9090");
  EXPECT_EQ(nodes[2].node_id, "c:9090");
}

TEST_F(StaticNodeDiscoveryTest, StopIsNoop) {
  auto discovery = std::make_unique<StaticNodeDiscovery>(
      std::vector<std::string>{"10.0.0.1:9090"});

  EXPECT_NO_THROW(discovery->Stop());
}

TEST_F(StaticNodeDiscoveryTest, FactoryCreatesDiscoveryWithDerivedIdentity) {
  strij::config::StaticNodeDiscoveryConfig config;
  config.add_addresses("10.0.0.1:9090");

  strij::extensions::MockFactoryContext context;
  StaticNodeDiscoveryFactory factory;
  auto discovery = factory.Create(config, context);

  auto nodes = CollectNodes(std::move(discovery));

  ASSERT_EQ(nodes.size(), 1U);
  EXPECT_EQ(nodes[0].node_id, "10.0.0.1:9090");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::extensions::node_discovery
