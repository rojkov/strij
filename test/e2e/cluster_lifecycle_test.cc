#include <signal.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "test/e2e/support/cluster.hh"
#include "test/e2e/support/cluster_spec.hh"
#include "test/e2e/support/environment.hh"
#include "test/e2e/support/process.hh"

#include "gtest/gtest.h"

namespace strij::e2e {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(ClusterLifecycleTest, ReachesReadinessForEveryGatewayAndNode) {
  RequireIoUring();
  if (testing::Test::IsSkipped()) {
    return;
  }

  Cluster cluster(DefaultFanoutSpec());
  const auto start = cluster.Start();
  ASSERT_TRUE(start.ok()) << start.message();

  const auto& observed = cluster.ObservedMarkers();
  ASSERT_EQ(observed.size(), cluster.Gateways().size());
  const std::set<std::string> expected = {"n0", "n1"};
  for (std::size_t gateway_index = 0; gateway_index < observed.size(); ++gateway_index) {
    EXPECT_TRUE(observed[gateway_index] == expected)
        << "gateway" << gateway_index << " observed " << observed[gateway_index].size()
        << " node marker(s)";
  }

  const auto stop = cluster.Stop();
  EXPECT_TRUE(stop.ok()) << stop.message();
}

TEST(ClusterLifecycleTest, CleanShutdownReapsEveryProcessAndReleasesPorts) {
  RequireIoUring();
  if (testing::Test::IsSkipped()) {
    return;
  }

  Cluster cluster(DefaultFanoutSpec());
  const auto start = cluster.Start();
  ASSERT_TRUE(start.ok()) << start.message();

  std::vector<uint16_t> ports;
  for (std::size_t index = 0; index < cluster.Nodes().size(); ++index) {
    ports.push_back(cluster.NodePort(index));
  }
  for (std::size_t index = 0; index < cluster.Gateways().size(); ++index) {
    ports.push_back(cluster.GatewayPort(index));
  }

  const auto stop = cluster.Stop();
  ASSERT_TRUE(stop.ok()) << stop.message();
  EXPECT_FALSE(cluster.Escalated());

  for (const auto& process : cluster.Nodes()) {
    EXPECT_EQ(DescendantsRemaining(process), 0U) << process.name;
  }
  for (const auto& process : cluster.Gateways()) {
    EXPECT_EQ(DescendantsRemaining(process), 0U) << process.name;
  }
  for (const auto port : ports) {
    EXPECT_FALSE(IsPortAccepting(port)) << "port " << port << " still accepts connections";
  }
}

TEST(ClusterLifecycleTest, StopEscalatesWhenAProcessDoesNotExit) {
  RequireIoUring();
  if (testing::Test::IsSkipped()) {
    return;
  }

  Cluster cluster(DefaultFanoutSpec());
  const auto start = cluster.Start();
  ASSERT_TRUE(start.ok()) << start.message();

  // SIGSTOP leaves the process unable to act on SIGTERM, so teardown must send
  // SIGKILL to the group and report escalation.
  ASSERT_FALSE(cluster.Nodes().empty());
  ASSERT_EQ(::killpg(cluster.Nodes()[0].pgid, SIGSTOP), 0);

  const auto stop = cluster.Stop();
  EXPECT_TRUE(stop.ok()) << stop.message();
  EXPECT_TRUE(cluster.Escalated());
}

TEST(ClusterLifecycleTest, InvalidNodeConfigurationFailsWithItsStartupError) {
  RequireIoUring();
  if (testing::Test::IsSkipped()) {
    return;
  }

  ClusterSpec spec = DefaultFanoutSpec();
  spec.nodes[0].include_pool = false;
  Cluster cluster(std::move(spec));

  const auto start = cluster.Start();
  ASSERT_FALSE(start.ok());
  EXPECT_NE(start.message().find("pools is empty"), std::string::npos) << start.message();
  EXPECT_NE(start.message().find("node0"), std::string::npos) << start.message();
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::e2e
