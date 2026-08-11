#include <memory>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "absl/status/status.h"
#include "core/config/extensions.pb.h"
#include "extensions/schedulers/round_robin/round_robin.pb.h"
#include "extensions/schedulers/scheduler.hh"
#include "gtest/gtest.h"

namespace strij::extensions::schedulers {
namespace {

class SchedulerFactoryTest : public ::testing::Test {
protected:
  std::shared_ptr<strij::event::MockDispatcher> dispatcher_{
      std::make_shared<strij::event::MockDispatcher>()};
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(SchedulerFactoryTest, MissingSchedulerConfigIsRejected) {
  strij::extensions::FactoryContextImpl context(dispatcher_);

  auto result = strij::extensions::CreateScheduler(nullptr, context);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(result.status().message().find("scheduler"), std::string::npos);
}

TEST_F(SchedulerFactoryTest, UnknownSchedulerNameIsRejected) {
  strij::extensions::FactoryContextImpl context(dispatcher_);
  strij::config::ExtensionConfig config;
  config.set_name("nonexistent");

  auto result = strij::extensions::CreateScheduler(&config, context);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
  EXPECT_NE(result.status().message().find("nonexistent"), std::string::npos);
}

TEST_F(SchedulerFactoryTest, CreatesRegisteredSchedulerFromConfig) {
  strij::extensions::FactoryContextImpl context(dispatcher_);
  strij::config::ExtensionConfig config;
  config.set_name("round_robin");
  config.mutable_typed_config()->PackFrom(
      strij::extensions::schedulers::round_robin::RoundRobinSchedulerConfig());

  auto result = strij::extensions::CreateScheduler(&config, context);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ((*result)->RequiredProtocol(), "push");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::extensions::schedulers
