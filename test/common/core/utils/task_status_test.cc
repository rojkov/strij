#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "common/core/utils/task_status.hh"
#include "gtest/gtest.h"

namespace strij::utils {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

// --- The success predicate ---

TEST(IsTaskResultOkTest, AbsentStatusReadsAsSuccess) {
  task::TaskResult result;
  result.set_id("t1");
  result.set_body("hello");

  EXPECT_TRUE(IsTaskResultOk(result));
}

TEST(IsTaskResultOkTest, ZeroMemberReadsAsSuccess) {
  task::TaskResult result;
  result.set_status(task::TASK_STATUS_OK);

  EXPECT_TRUE(IsTaskResultOk(result));
}

TEST(IsTaskResultOkTest, AnyOtherMemberReadsAsFailure) {
  task::TaskResult result;
  result.set_status(task::TASK_STATUS_INTERNAL);
  result.set_body("boom");

  EXPECT_FALSE(IsTaskResultOk(result));
}

TEST(IsTaskResultOkTest, UnnamedNonZeroValueReadsAsFailure) {
  task::TaskResult result;
  result.set_status(static_cast<task::TaskStatus>(999));

  EXPECT_FALSE(IsTaskResultOk(result));
}

// --- Round-trip guarantees for the field ---

TEST(TaskStatusFieldTest, AbsentStatusSurvivesRoundTripAsUnset) {
  task::TaskResult result;
  result.set_id("t1");

  std::string serialized;
  ASSERT_TRUE(result.SerializeToString(&serialized));

  task::TaskResult parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  EXPECT_FALSE(parsed.has_status());
  EXPECT_TRUE(IsTaskResultOk(parsed));
}

TEST(TaskStatusFieldTest, UnnamedMemberSurvivesRoundTripAsFailure) {
  task::TaskResult result;
  result.set_id("t1");
  result.set_body("from a newer producer");
  result.set_status(static_cast<task::TaskStatus>(42));

  std::string serialized;
  ASSERT_TRUE(result.SerializeToString(&serialized));

  task::TaskResult parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_status());
  EXPECT_EQ(static_cast<int>(parsed.status()), 42);
  EXPECT_FALSE(IsTaskResultOk(parsed));
}

// --- The code -> HTTP mapping ---

// The retryability intent of the code set, stated as sets of HTTP statuses so
// a test can assert which side of the boundary a code falls on.
constexpr std::array kNonRetryableStatuses{400, 404, 501};
constexpr std::array kRetryableStatuses{429, 503, 504};

TEST(TaskStatusToHttpStatusTest, OkMapsTo200) {
  EXPECT_EQ(TaskStatusToHttpStatus(task::TASK_STATUS_OK), 200);
}

TEST(TaskStatusToHttpStatusTest, MalformedRequestMapsToNonRetryableClientError) {
  const int status = TaskStatusToHttpStatus(task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_EQ(status, 400);
  EXPECT_TRUE(std::ranges::contains(kNonRetryableStatuses, status));
}

TEST(TaskStatusToHttpStatusTest, NotFoundMapsToNonRetryableClientError) {
  const int status = TaskStatusToHttpStatus(task::TASK_STATUS_NOT_FOUND);
  EXPECT_EQ(status, 404);
  EXPECT_TRUE(std::ranges::contains(kNonRetryableStatuses, status));
}

TEST(TaskStatusToHttpStatusTest, UnimplementedMapsToNonRetryableClientError) {
  const int status = TaskStatusToHttpStatus(task::TASK_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(status, 501);
  EXPECT_TRUE(std::ranges::contains(kNonRetryableStatuses, status));
}

TEST(TaskStatusToHttpStatusTest, DeadlineExceededMapsToRetryableStatus) {
  const int status = TaskStatusToHttpStatus(task::TASK_STATUS_DEADLINE_EXCEEDED);
  EXPECT_EQ(status, 504);
  EXPECT_TRUE(std::ranges::contains(kRetryableStatuses, status));
}

// Pinned by its own case so a later change to 429 is deliberate (task 2.3).
TEST(TaskStatusToHttpStatusTest, CapacityRefusedMapsTo429) {
  EXPECT_EQ(TaskStatusToHttpStatus(task::TASK_STATUS_CAPACITY_REFUSED), 429);
}

// Pinned by its own case so a later change to 503 is deliberate (task 2.3).
TEST(TaskStatusToHttpStatusTest, TemporarilyUnavailableMapsTo503) {
  EXPECT_EQ(TaskStatusToHttpStatus(task::TASK_STATUS_UNAVAILABLE), 503);
}

TEST(TaskStatusToHttpStatusTest, CapacityRefusedAndUnavailableAreDistinguishable) {
  // The old hardcoded 503 could not express this pair.
  EXPECT_NE(TaskStatusToHttpStatus(task::TASK_STATUS_CAPACITY_REFUSED),
            TaskStatusToHttpStatus(task::TASK_STATUS_UNAVAILABLE));
}

TEST(TaskStatusToHttpStatusTest, InternalMapsTo500) {
  EXPECT_EQ(TaskStatusToHttpStatus(task::TASK_STATUS_INTERNAL), 500);
}

// Every member this schema declares, in declaration order.
constexpr std::array kDeclaredStatuses{
    task::TASK_STATUS_OK,
    task::TASK_STATUS_MALFORMED_REQUEST,
    task::TASK_STATUS_NOT_FOUND,
    task::TASK_STATUS_UNIMPLEMENTED,
    task::TASK_STATUS_DEADLINE_EXCEEDED,
    task::TASK_STATUS_CAPACITY_REFUSED,
    task::TASK_STATUS_INTERNAL,
    task::TASK_STATUS_UNAVAILABLE,
};

TEST(TaskStatusToHttpStatusTest, EveryDeclaredMemberMaps) {
  for (const task::TaskStatus status : kDeclaredStatuses) {
    EXPECT_GT(TaskStatusToHttpStatus(status), 0) << static_cast<int>(status);
  }
}

TEST(TaskStatusToHttpStatusTest, MappingIsInjectiveOnDeclaredSet) {
  std::vector<int> mapped;
  mapped.reserve(kDeclaredStatuses.size());
  for (const task::TaskStatus status : kDeclaredStatuses) {
    mapped.push_back(TaskStatusToHttpStatus(status));
  }
  std::ranges::sort(mapped);
  EXPECT_EQ(std::ranges::unique(mapped).begin(), mapped.end());
}

TEST(TaskStatusToHttpStatusTest, UnnamedValueMapsToInternalFailureNever200) {
  const int status = TaskStatusToHttpStatus(static_cast<task::TaskStatus>(42));
  EXPECT_EQ(status, 500);
  EXPECT_NE(status, 200);
}

TEST(HttpStatusPhraseTest, PhrasesMatchTheirStatuses) {
  EXPECT_EQ(HttpStatusPhrase(200), "OK");
  EXPECT_EQ(HttpStatusPhrase(400), "Bad Request");
  EXPECT_EQ(HttpStatusPhrase(404), "Not Found");
  EXPECT_EQ(HttpStatusPhrase(429), "Too Many Requests");
  EXPECT_EQ(HttpStatusPhrase(500), "Internal Server Error");
  EXPECT_EQ(HttpStatusPhrase(501), "Not Implemented");
  EXPECT_EQ(HttpStatusPhrase(503), "Service Unavailable");
  EXPECT_EQ(HttpStatusPhrase(504), "Gateway Timeout");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::utils
