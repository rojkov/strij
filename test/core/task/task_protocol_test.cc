#include <string>

#include "core/task/task.pb.h"
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

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::task
