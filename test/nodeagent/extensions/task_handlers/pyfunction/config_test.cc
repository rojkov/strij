#include <string>

#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "nodeagent/extensions/task_handlers/pyfunction/config.hh"
#include "nodeagent/extensions/task_handlers/pyfunction/pyfunction.pb.h"

namespace strij::nodeagent::task_handlers::pyfunction {
namespace {

using ConfigProto = ::strij::extensions::task_handlers::pyfunction::PyFunctionTaskHandlerConfig;

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(PyFunctionConfigTest, AcceptsValidConfiguration) {
  ConfigProto proto;
  proto.mutable_capacity()->set_concurrency(3);
  proto.set_worker_module("strij._worker");
  proto.set_python_executable("python3");
  proto.set_max_frame_bytes(1U << 20U);
  auto parsed = ParsePyFunctionConfig(proto);
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(parsed->concurrency_, 3U);
  EXPECT_EQ(parsed->worker_module_, "strij._worker");
}

TEST(PyFunctionConfigTest, RejectsZeroConcurrency) {
  ConfigProto proto;
  proto.set_worker_module("strij._worker");
  proto.set_max_frame_bytes(1U << 20U);
  auto parsed = ParsePyFunctionConfig(proto);
  EXPECT_FALSE(parsed.ok());
  EXPECT_NE(parsed.status().message().find("concurrency"), std::string::npos);
}

TEST(PyFunctionConfigTest, RejectsMissingWorkerTarget) {
  ConfigProto proto;
  proto.mutable_capacity()->set_concurrency(1);
  proto.set_max_frame_bytes(1U << 20U);
  auto parsed = ParsePyFunctionConfig(proto);
  EXPECT_FALSE(parsed.ok());
  EXPECT_NE(parsed.status().message().find("worker"), std::string::npos);
}

TEST(PyFunctionConfigTest, RejectsBothWorkerTargets) {
  ConfigProto proto;
  proto.mutable_capacity()->set_concurrency(1);
  proto.set_worker_module("strij._worker");
  proto.set_worker_executable("/bin/worker");
  proto.set_max_frame_bytes(1U << 20U);
  auto parsed = ParsePyFunctionConfig(proto);
  EXPECT_FALSE(parsed.ok());
}

TEST(PyFunctionConfigTest, RejectsNegativeTimeout) {
  ConfigProto proto;
  proto.mutable_capacity()->set_concurrency(1);
  proto.set_worker_module("strij._worker");
  proto.mutable_startup_timeout()->set_seconds(-1);
  proto.set_max_frame_bytes(1U << 20U);
  auto parsed = ParsePyFunctionConfig(proto);
  EXPECT_FALSE(parsed.ok());
  EXPECT_NE(parsed.status().message().find("negative"), std::string::npos);
}

TEST(PyFunctionConfigTest, DefaultsTimeouts) {
  ConfigProto proto;
  proto.mutable_capacity()->set_concurrency(1);
  proto.set_worker_module("strij._worker");
  proto.set_max_frame_bytes(1U << 20U);
  auto parsed = ParsePyFunctionConfig(proto);
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(parsed->startup_timeout_, absl::Seconds(30));
  EXPECT_EQ(parsed->task_timeout_, absl::ZeroDuration());
  EXPECT_EQ(parsed->remote_call_timeout_, absl::Seconds(300));
}

TEST(PyFunctionConfigTest, RejectsZeroFrameBound) {
  ConfigProto proto;
  proto.mutable_capacity()->set_concurrency(1);
  proto.set_worker_module("strij._worker");
  auto parsed = ParsePyFunctionConfig(proto);
  EXPECT_FALSE(parsed.ok());
  EXPECT_NE(parsed.status().message().find("max_frame_bytes"), std::string::npos);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::pyfunction
