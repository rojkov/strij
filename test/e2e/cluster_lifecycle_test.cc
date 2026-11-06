#include <signal.h>

#include <chrono>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "test/e2e/support/cluster.hh"
#include "test/e2e/support/cluster_spec.hh"
#include "test/e2e/support/environment.hh"
#include "test/e2e/support/http_client.hh"
#include "test/e2e/support/process.hh"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace strij::e2e {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

constexpr std::string_view kPipedExecutable = "piped_executable";

TEST(ClusterLifecycleTest, ReachesReadinessForEveryGatewayAndNode) {
  RequireIoUring();
  if (testing::Test::IsSkipped()) {
    return;
  }

  Cluster cluster(DefaultFanoutSpec());
  const auto start = cluster.Start();
  ASSERT_TRUE(start.ok()) << start.message();

  const auto& observed = cluster.ObservedMarkers();
  ASSERT_EQ(observed.size(), cluster.Gateways().size()) << cluster.Diagnose();
  const std::set<std::string> expected = {"n0", "n1"};
  for (std::size_t gateway_index = 0; gateway_index < observed.size(); ++gateway_index) {
    EXPECT_TRUE(observed[gateway_index] == expected)
        << "gateway" << gateway_index << " observed " << observed[gateway_index].size()
        << " node marker(s)\n"
        << cluster.Diagnose();
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
  EXPECT_FALSE(cluster.Escalated()) << cluster.Diagnose();

  for (const auto& process : cluster.Nodes()) {
    EXPECT_EQ(DescendantsRemaining(process), 0U) << process.name << "\n" << cluster.Diagnose();
  }
  for (const auto& process : cluster.Gateways()) {
    EXPECT_EQ(DescendantsRemaining(process), 0U) << process.name << "\n" << cluster.Diagnose();
  }
  for (const auto port : ports) {
    EXPECT_FALSE(IsPortAccepting(port)) << "port " << port << " still accepts connections\n"
                                        << cluster.Diagnose();
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
  EXPECT_TRUE(cluster.Escalated()) << cluster.Diagnose();
}

TEST(ClusterLifecycleTest, TaskGrandchildrenDoNotSurviveClusterStop) {
  RequireIoUring();
  if (testing::Test::IsSkipped()) {
    return;
  }

  Cluster cluster(DefaultFanoutSpec());
  const auto start = cluster.Start();
  ASSERT_TRUE(start.ok()) << start.message();

  // The task's shell intentionally leaves a background child behind (see
  // markerScript in cluster_spec.cc). The harness teardown must remove it
  // through the node's process group.
  const auto response =
      Post(cluster.GatewayPort(0), absl::StrCat("/tasks/", kPipedExecutable), "spawn-child",
           {{"x-strij-function", "marker.sh"}}, std::chrono::seconds{10});
  ASSERT_TRUE(response.ok()) << response.status().message() << "\n" << cluster.Diagnose();
  ASSERT_EQ(response->status_code, 200) << response->body << "\n" << cluster.Diagnose();

  const auto descendants = [&cluster]() -> std::size_t {
    std::size_t total = 0;
    for (const auto& node : cluster.Nodes()) {
      total += DescendantsRemaining(node);
    }
    return total;
  };
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (descendants() == 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  EXPECT_GT(descendants(), 0U) << "the task's background child was not observed before teardown\n"
                               << cluster.Diagnose();

  const auto stop = cluster.Stop();
  ASSERT_TRUE(stop.ok()) << stop.message();

  for (const auto& node : cluster.Nodes()) {
    EXPECT_EQ(DescendantsRemaining(node), 0U) << node.name << " left a task descendant running\n"
                                              << cluster.Diagnose();
  }
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
