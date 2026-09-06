#include <string>

#include "common/task/probe.pb.h"
#include "common/node/capabilities.pb.h"
#include "gtest/gtest.h"

namespace strij::task {
namespace {

task::TaskProbe MakeProbe() {
  task::TaskProbe probe;
  probe.set_id("probe_fox_77");
  probe.set_type("echo");
  auto* resources = probe.mutable_requirements();
  (*resources->mutable_resources())["cpu"] = 2;
  return probe;
}

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(TaskProbeTest, CarriesIdTypeAndRequirementsWithoutBody) {
  task::TaskProbe probe = MakeProbe();

  std::string serialized;
  probe.SerializeToString(&serialized);

  task::TaskProbe parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  EXPECT_EQ(parsed.id(), "probe_fox_77");
  EXPECT_EQ(parsed.type(), "echo");
  ASSERT_TRUE(parsed.has_requirements());
  ASSERT_EQ(parsed.requirements().resources_size(), 1);
  EXPECT_EQ(parsed.requirements().resources().at("cpu"), 2U);
  // The probe message has no body field by construction.
}

TEST(TaskPullTest, CarriesClaimedTaskId) {
  task::TaskPull pull;
  pull.set_id("probe_fox_77");

  std::string serialized;
  pull.SerializeToString(&serialized);

  task::TaskPull parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  EXPECT_EQ(parsed.id(), "probe_fox_77");
}

TEST(TaskProbeCancelTest, CarriesRelinquishedTaskId) {
  task::TaskProbeCancel cancel;
  cancel.set_id("probe_fox_77");

  std::string serialized;
  cancel.SerializeToString(&serialized);

  task::TaskProbeCancel parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  EXPECT_EQ(parsed.id(), "probe_fox_77");
}

TEST(TaskDeclineTest, CarriesIdAndReason) {
  task::TaskDecline decline;
  decline.set_id("probe_fox_77");
  decline.set_reason("queue full");

  std::string serialized;
  decline.SerializeToString(&serialized);

  task::TaskDecline parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  EXPECT_EQ(parsed.id(), "probe_fox_77");
  EXPECT_EQ(parsed.reason(), "queue full");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::task