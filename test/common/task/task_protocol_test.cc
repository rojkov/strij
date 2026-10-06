#include <string>

#include "common/core/utils/task_status.hh"
#include "common/task/task.pb.h"
#include "gtest/gtest.h"

namespace strij::task {
namespace {

// Consumers treat absence of is_final as final (proposal: absence ⇒ final).
auto IsFinal(const TaskResult& result) -> bool {
  return !result.has_is_final() || result.is_final();
}

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(TaskResultTest, AbsentIsFinalRoundTripsAsUnset) {
  TaskResult result;
  result.set_id("42");
  result.set_body("hello");
  ASSERT_FALSE(result.has_is_final());

  std::string serialized;
  result.SerializeToString(&serialized);

  TaskResult parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  EXPECT_FALSE(parsed.has_is_final());
  // proto3 bool default is false, so "final" must be derived from absence.
  EXPECT_FALSE(parsed.is_final());
  EXPECT_TRUE(IsFinal(parsed));
}

TEST(TaskResultTest, ExplicitIsFinalFalseRoundTrips) {
  TaskResult result;
  result.set_id("42");
  result.set_is_final(false);

  std::string serialized;
  result.SerializeToString(&serialized);

  TaskResult parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  EXPECT_TRUE(parsed.has_is_final());
  EXPECT_FALSE(parsed.is_final());
  EXPECT_FALSE(IsFinal(parsed));
}

TEST(TaskResultTest, ExplicitIsFinalTrueRoundTrips) {
  TaskResult result;
  result.set_is_final(true);

  std::string serialized;
  result.SerializeToString(&serialized);

  TaskResult parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  EXPECT_TRUE(parsed.has_is_final());
  EXPECT_TRUE(IsFinal(parsed));
}

TEST(TaskResultTest, AbsentStatusRoundTripsAsUnsetAndReadsAsSuccess) {
  TaskResult result;
  result.set_id("42");
  result.set_body("hello");
  ASSERT_FALSE(result.has_status());

  std::string serialized;
  ASSERT_TRUE(result.SerializeToString(&serialized));

  TaskResult parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  EXPECT_FALSE(parsed.has_status());
  EXPECT_TRUE(utils::IsTaskResultOk(parsed));
}

TEST(TaskResultTest, FailedResultCarriesStatusAndErrorBody) {
  TaskResult result;
  result.set_id("42");
  result.set_body("gpu pool exhausted");
  result.set_status(TASK_STATUS_CAPACITY_REFUSED);

  std::string serialized;
  ASSERT_TRUE(result.SerializeToString(&serialized));

  TaskResult parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_status());
  EXPECT_EQ(parsed.status(), TASK_STATUS_CAPACITY_REFUSED);
  EXPECT_FALSE(utils::IsTaskResultOk(parsed));
  EXPECT_EQ(parsed.body(), "gpu pool exhausted");
}

TEST(TaskResultTest, UnnamedStatusValueSurvivesRoundTripAsFailure) {
  TaskResult result;
  result.set_id("42");
  // A value this schema does not name, as a newer producer would send.
  result.set_status(static_cast<TaskStatus>(42));

  std::string serialized;
  ASSERT_TRUE(result.SerializeToString(&serialized));

  TaskResult parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_status());
  EXPECT_EQ(static_cast<int>(parsed.status()), 42);
  EXPECT_FALSE(utils::IsTaskResultOk(parsed));
}

TEST(TaskTest, ParametersRoundTrip) {
  Task task;
  task.set_id("42");
  task.set_type("echo");
  task.set_body("hello");
  (*task.mutable_parameters())["function"] = "/usr/bin/cat";

  std::string serialized;
  task.SerializeToString(&serialized);

  Task parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  EXPECT_EQ(parsed.id(), "42");
  EXPECT_EQ(parsed.type(), "echo");
  EXPECT_EQ(parsed.body(), "hello");
  EXPECT_EQ(parsed.parameters_size(), 1);
  EXPECT_EQ(parsed.parameters().at("function"), "/usr/bin/cat");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::task
