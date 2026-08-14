#include <string>

#include "core/gateway/exact_state_tracker.hh"
#include "core/node/capabilities.pb.h"
#include "gtest/gtest.h"

namespace strij::gateway {
namespace {

auto Resources(std::initializer_list<std::pair<std::string, uint64_t>> entries)
    -> strij::node::ResourceRequirements {
  strij::node::ResourceRequirements requirements;
  for (const auto& [pool, amount] : entries) {
    requirements.mutable_resources()->insert({pool, amount});
  }
  return requirements;
}

TEST(ExactStateTrackerTest, IncrementsOnSend) {
  ExactStateTracker tracker;
  tracker.RecordSubmission("t1", "node-a", Resources({{"cpu", 2}, {"gpu.h100", 1}}));

  EXPECT_EQ(tracker.InFlight("node-a"), 1U);
  EXPECT_EQ(tracker.PoolInUse("node-a", "cpu"), 2U);
  EXPECT_EQ(tracker.PoolInUse("node-a", "gpu.h100"), 1U);
}

TEST(ExactStateTrackerTest, DecrementsOnFinalResult) {
  ExactStateTracker tracker;
  tracker.RecordSubmission("t1", "node-a", Resources({{"cpu", 2}}));
  tracker.RecordSubmission("t2", "node-a", Resources({{"cpu", 1}}));

  tracker.RecordCompletion("t1");

  EXPECT_EQ(tracker.InFlight("node-a"), 1U);
  EXPECT_EQ(tracker.PoolInUse("node-a", "cpu"), 1U);
}

TEST(ExactStateTrackerTest, CompletionForUnknownTaskIsNoop) {
  ExactStateTracker tracker;
  tracker.RecordSubmission("t1", "node-a", Resources({{"cpu", 2}}));

  tracker.RecordCompletion("does-not-exist");

  EXPECT_EQ(tracker.InFlight("node-a"), 1U);
  EXPECT_EQ(tracker.PoolInUse("node-a", "cpu"), 2U);
}

TEST(ExactStateTrackerTest, TracksAcrossNodesIndependently) {
  ExactStateTracker tracker;
  tracker.RecordSubmission("t1", "node-a", Resources({{"cpu", 2}}));
  tracker.RecordSubmission("t2", "node-b", Resources({{"cpu", 4}}));

  EXPECT_EQ(tracker.InFlight("node-a"), 1U);
  EXPECT_EQ(tracker.PoolInUse("node-a", "cpu"), 2U);
  EXPECT_EQ(tracker.InFlight("node-b"), 1U);
  EXPECT_EQ(tracker.PoolInUse("node-b", "cpu"), 4U);
}

TEST(ExactStateTrackerTest, ApplyStateSnapshotCorrectsExactCounts) {
  ExactStateTracker tracker;
  tracker.RecordSubmission("t1", "node-a", Resources({{"cpu", 2}}));
  tracker.RecordSubmission("t2", "node-a", Resources({{"cpu", 1}}));
  // Simulate drift: exact accounting says 2 tasks in flight.
  EXPECT_EQ(tracker.InFlight("node-a"), 2U);

  strij::node::NodeState state;
  state.set_node_id("node-a");
  state.set_in_flight(1);
  auto* usage = state.add_pools();
  usage->set_pool("cpu");
  usage->set_in_use(1);
  tracker.ApplyStateSnapshot(state);

  EXPECT_EQ(tracker.InFlight("node-a"), 1U);
  EXPECT_EQ(tracker.PoolInUse("node-a", "cpu"), 1U);
}

TEST(ExactStateTrackerTest, UnknownNodeReportsZero) {
  ExactStateTracker tracker;
  EXPECT_EQ(tracker.InFlight("nope"), 0U);
  EXPECT_EQ(tracker.PoolInUse("nope", "cpu"), 0U);
}

} // namespace
} // namespace strij::gateway
