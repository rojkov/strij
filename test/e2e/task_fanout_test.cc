#include <chrono>
#include <cstddef>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "test/e2e/support/cluster.hh"
#include "test/e2e/support/cluster_spec.hh"
#include "test/e2e/support/environment.hh"
#include "test/e2e/support/http_client.hh"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace strij::e2e {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

constexpr std::string_view kPipedExecutable = "piped_executable";
constexpr std::string_view kFunctionHeader = "x-strij-function";
constexpr auto kRequestTimeout = std::chrono::seconds{10};

auto markerBody(std::string_view marker) -> std::string {
  return absl::StrCat("{\"node\":\"", marker, "\"}");
}

TEST(TaskFanoutTest, EveryGatewayServesEveryNode) {
  RequireIoUring();
  if (testing::Test::IsSkipped()) {
    return;
  }

  Cluster cluster(DefaultFanoutSpec());
  const auto start = cluster.Start();
  ASSERT_TRUE(start.ok()) << start.message();

  const auto& observed = cluster.ObservedMarkers();
  ASSERT_EQ(observed.size(), 2U) << cluster.Diagnose();
  for (std::size_t gateway_index = 0; gateway_index < observed.size(); ++gateway_index) {
    EXPECT_EQ(observed[gateway_index].size(), 2U) << "gateway" << gateway_index << "\n"
                                                  << cluster.Diagnose();
    EXPECT_TRUE(observed[gateway_index].contains("n0")) << "gateway" << gateway_index << "\n"
                                                        << cluster.Diagnose();
    EXPECT_TRUE(observed[gateway_index].contains("n1")) << "gateway" << gateway_index << "\n"
                                                        << cluster.Diagnose();
  }

  const auto stop = cluster.Stop();
  EXPECT_TRUE(stop.ok()) << stop.message();
}

TEST(TaskFanoutTest, ConcurrentRequestsThroughBothGatewaysMatchTheirOwnBodies) {
  RequireIoUring();
  if (testing::Test::IsSkipped()) {
    return;
  }

  Cluster cluster(DefaultFanoutSpec());
  const auto start = cluster.Start();
  ASSERT_TRUE(start.ok()) << start.message();

  constexpr std::size_t kRequestCount = 40;
  std::vector<absl::StatusOr<HttpResponse>> responses(kRequestCount,
                                                      absl::InternalError("request was not run"));

  std::vector<std::thread> threads;
  threads.reserve(kRequestCount);
  for (std::size_t index = 0; index < kRequestCount; ++index) {
    threads.emplace_back([&cluster, &responses, index]() {
      const std::size_t gateway_index = index % cluster.Gateways().size();
      const std::string body = "body-" + std::to_string(index);
      responses[index] =
          Post(cluster.GatewayPort(gateway_index), absl::StrCat("/tasks/", kPipedExecutable), body,
               {{std::string(kFunctionHeader), "/bin/cat"}}, kRequestTimeout);
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  for (std::size_t index = 0; index < kRequestCount; ++index) {
    ASSERT_TRUE(responses[index].ok())
        << "request " << index << ": " << responses[index].status().message() << "\n"
        << cluster.Diagnose();
    EXPECT_EQ(responses[index]->status_code, 200)
        << "request " << index << ": " << responses[index]->body << "\n"
        << cluster.Diagnose();
    EXPECT_EQ(responses[index]->body, "body-" + std::to_string(index))
        << "request " << index << "\n"
        << cluster.Diagnose();
  }

  const auto stop = cluster.Stop();
  EXPECT_TRUE(stop.ok()) << stop.message();
}

TEST(TaskFanoutTest, PerNodeCapacityAdmitsOneUnitPerNode) {
  RequireIoUring();
  if (testing::Test::IsSkipped()) {
    return;
  }

  ClusterSpec spec = DefaultFanoutSpec();
  for (auto& node : spec.nodes) {
    node.pool_total = 1;
  }

  Cluster cluster(std::move(spec));
  const auto start = cluster.Start();
  ASSERT_TRUE(start.ok()) << start.message();

  constexpr std::size_t kRequestCount = 4;
  std::vector<absl::StatusOr<HttpResponse>> responses(kRequestCount,
                                                      absl::InternalError("request was not run"));

  std::vector<std::thread> threads;
  threads.reserve(kRequestCount);
  for (std::size_t index = 0; index < kRequestCount; ++index) {
    threads.emplace_back([&cluster, &responses, index]() {
      const std::size_t gateway_index = index % cluster.Gateways().size();
      responses[index] = Post(
          cluster.GatewayPort(gateway_index), absl::StrCat("/tasks/", kPipedExecutable), "slow",
          {{std::string(kFunctionHeader), "marker.sh"}, {"x-strij-resources.cpu", "1"}},
          kRequestTimeout);
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  std::size_t successes = 0;
  std::size_t refusals = 0;
  std::set<std::string> served_markers;
  for (std::size_t index = 0; index < kRequestCount; ++index) {
    ASSERT_TRUE(responses[index].ok())
        << "request " << index << ": " << responses[index].status().message() << "\n"
        << cluster.Diagnose();
    if (responses[index]->status_code == 200) {
      ++successes;
      served_markers.insert(responses[index]->body);
    } else if (responses[index]->status_code == 429) {
      ++refusals;
    } else {
      ADD_FAILURE() << "request " << index << " returned HTTP " << responses[index]->status_code
                    << ": " << responses[index]->body << "\n"
                    << cluster.Diagnose();
    }
  }

  EXPECT_EQ(successes, 2U) << cluster.Diagnose();
  EXPECT_EQ(refusals, 2U) << cluster.Diagnose();
  EXPECT_TRUE(served_markers.contains(markerBody("n0"))) << "no success from node n0\n"
                                                         << cluster.Diagnose();
  EXPECT_TRUE(served_markers.contains(markerBody("n1"))) << "no success from node n1\n"
                                                         << cluster.Diagnose();

  const auto stop = cluster.Stop();
  EXPECT_TRUE(stop.ok()) << stop.message();
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::e2e
