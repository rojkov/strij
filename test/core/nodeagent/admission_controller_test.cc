#include <string>

#include "core/node/capabilities.pb.h"
#include "core/nodeagent/admission_controller.hh"
#include "gtest/gtest.h"

namespace strij::nodeagent {
namespace {

auto MakeCapabilities() -> strij::node::NodeCapabilities {
  strij::node::NodeCapabilities caps;
  caps.set_node_id("node-test");
  caps.set_capability_version(1);
  return caps;
}

auto Resources(std::initializer_list<std::pair<std::string, uint64_t>> entries)
    -> strij::node::ResourceRequirements {
  strij::node::ResourceRequirements requirements;
  for (const auto& [pool, amount] : entries) {
    requirements.mutable_resources()->insert({pool, amount});
  }
  return requirements;
}

class AdmissionControllerTest : public ::testing::Test {
protected:
  AdmissionController controller_{MakeCapabilities()};
};

TEST_F(AdmissionControllerTest, AdmissionReservesCapacity) {
  auto caps = MakeCapabilities();
  auto* pool = caps.add_pools();
  pool->set_name("cpu");
  pool->set_total(16);

  AdmissionController controller{caps};
  EXPECT_EQ(controller.SharedFree("cpu"), 16U);

  auto status = controller.Admit("echo", Resources({{"cpu", 2}}));
  ASSERT_TRUE(status.ok()) << status.message();

  EXPECT_EQ(controller.SharedFree("cpu"), 14U);
  EXPECT_EQ(controller.InFlight("echo"), 1U);

  auto snapshot = controller.BuildStateSnapshot("node-test", 1);
  EXPECT_EQ(snapshot.in_flight(), 1U);
  ASSERT_EQ(snapshot.pools_size(), 1);
  EXPECT_EQ(snapshot.pools(0).pool(), "cpu");
  EXPECT_EQ(snapshot.pools(0).in_use(), 2U);
}

TEST_F(AdmissionControllerTest, CompletionReleasesCapacity) {
  auto caps = MakeCapabilities();
  auto* pool = caps.add_pools();
  pool->set_name("cpu");
  pool->set_total(16);

  AdmissionController controller{caps};
  ASSERT_TRUE(controller.Admit("echo", Resources({{"cpu", 2}})).ok());

  controller.Release("echo", Resources({{"cpu", 2}}));

  EXPECT_EQ(controller.SharedFree("cpu"), 16U);
  EXPECT_EQ(controller.InFlight("echo"), 0U);
  EXPECT_EQ(controller.BuildStateSnapshot("node-test", 1).pools(0).in_use(), 0U);
}

TEST_F(AdmissionControllerTest, ReservationsAreExcludedFromSharedCapacity) {
  auto caps = MakeCapabilities();
  auto* pool = caps.add_pools();
  pool->set_name("gpu.h100");
  pool->set_total(2);
  auto* reservation = caps.add_reservations();
  reservation->set_task_type("video-encode");
  reservation->set_pool("gpu.h100");
  reservation->set_amount(1);

  AdmissionController controller{caps};

  EXPECT_EQ(controller.SharedFree("gpu.h100"), 1U);
  ASSERT_TRUE(controller.Admit("echo", Resources({{"gpu.h100", 1}})).ok());
  EXPECT_EQ(controller.SharedFree("gpu.h100"), 0U);
}

TEST_F(AdmissionControllerTest, RejectsWhenPoolExhausted) {
  auto caps = MakeCapabilities();
  auto* pool = caps.add_pools();
  pool->set_name("gpu.h100");
  pool->set_total(1);

  AdmissionController controller{caps};
  ASSERT_TRUE(controller.Admit("echo", Resources({{"gpu.h100", 1}})).ok());

  auto status = controller.Admit("echo", Resources({{"gpu.h100", 1}}));
  EXPECT_FALSE(status.ok());
  EXPECT_NE(status.message().find("gpu.h100"), std::string::npos);
  EXPECT_EQ(controller.SharedFree("gpu.h100"), 0U);
}

TEST_F(AdmissionControllerTest, RejectsAtConcurrencyLimit) {
  auto caps = MakeCapabilities();
  auto* handler = caps.add_handlers();
  handler->set_task_type("echo");
  handler->set_concurrency(1);

  AdmissionController controller{caps};
  ASSERT_TRUE(controller.Admit("echo", {}).ok());

  auto status = controller.Admit("echo", {});
  EXPECT_FALSE(status.ok());
  EXPECT_NE(status.message().find("concurrency limit"), std::string::npos);
  EXPECT_EQ(controller.InFlight("echo"), 1U);
}

TEST_F(AdmissionControllerTest, ZeroConcurrencyMeansNoLimit) {
  auto caps = MakeCapabilities();
  auto* handler = caps.add_handlers();
  handler->set_task_type("echo");
  handler->set_concurrency(0);

  AdmissionController controller{caps};
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(controller.Admit("echo", {}).ok()) << "iteration " << i;
  }
  EXPECT_EQ(controller.InFlight("echo"), 100U);
}

TEST_F(AdmissionControllerTest, RejectsUndeclaredPool) {
  auto caps = MakeCapabilities();
  auto* pool = caps.add_pools();
  pool->set_name("cpu");
  pool->set_total(16);

  AdmissionController controller{caps};
  auto status = controller.Admit("echo", Resources({{"mem", 1024}}));
  EXPECT_FALSE(status.ok());
  EXPECT_NE(status.message().find("undeclared pool"), std::string::npos);
}

TEST_F(AdmissionControllerTest, SnapshotCarriesPoolAndTypeUsage) {
  auto caps = MakeCapabilities();
  auto* pool = caps.add_pools();
  pool->set_name("cpu");
  pool->set_total(16);
  auto* handler = caps.add_handlers();
  handler->set_task_type("echo");
  handler->set_concurrency(8);

  AdmissionController controller{caps};
  ASSERT_TRUE(controller.Admit("echo", Resources({{"cpu", 2}})).ok());
  ASSERT_TRUE(controller.Admit("echo", Resources({{"cpu", 3}})).ok());

  auto snapshot = controller.BuildStateSnapshot("node-test", 42);
  EXPECT_EQ(snapshot.node_id(), "node-test");
  EXPECT_EQ(snapshot.seq(), 42U);
  EXPECT_EQ(snapshot.in_flight(), 2U);
  ASSERT_EQ(snapshot.pools_size(), 1);
  EXPECT_EQ(snapshot.pools(0).in_use(), 5U);
  ASSERT_EQ(snapshot.type_usage_size(), 1);
  EXPECT_EQ(snapshot.type_usage(0).task_type(), "echo");
  EXPECT_EQ(snapshot.type_usage(0).in_flight(), 2U);
}

TEST_F(AdmissionControllerTest, UnknownPoolReportsZeroSharedFree) {
  AdmissionController controller{MakeCapabilities()};
  EXPECT_EQ(controller.SharedFree("nope"), 0U);
}

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(AdmissionControllerTest, ScopeReleasesOnDestruction) {
  auto caps = MakeCapabilities();
  auto* pool = caps.add_pools();
  pool->set_name("cpu");
  pool->set_total(4);

  auto controller = std::make_shared<AdmissionController>(caps);
  ASSERT_TRUE(controller->Admit("echo", Resources({{"cpu", 1}})).ok());
  {
    AdmissionScope scope(controller, "echo", Resources({{"cpu", 1}}));
    EXPECT_EQ(controller->SharedFree("cpu"), 3U);
  }
  EXPECT_EQ(controller->SharedFree("cpu"), 4U);
}

TEST_F(AdmissionControllerTest, ScopeReleaseIsIdempotent) {
  auto caps = MakeCapabilities();
  auto* pool = caps.add_pools();
  pool->set_name("cpu");
  pool->set_total(4);

  auto controller = std::make_shared<AdmissionController>(caps);
  ASSERT_TRUE(controller->Admit("echo", Resources({{"cpu", 1}})).ok());
  AdmissionScope scope(controller, "echo", Resources({{"cpu", 1}}));
  scope.Release();
  scope.Release();
  EXPECT_EQ(controller->SharedFree("cpu"), 4U);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent
