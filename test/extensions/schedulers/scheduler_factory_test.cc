#include <memory>

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
  std::shared_ptr<event::MockDispatcher> dispatcher_{std::make_shared<event::MockDispatcher>()};
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(SchedulerFactoryTest, MissingSchedulerConfigIsRejected) {
  extensions::FactoryContextImpl context(dispatcher_);

  auto result = extensions::CreateScheduler(nullptr, context);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(result.status().message().find("scheduler"), std::string::npos);
}

TEST_F(SchedulerFactoryTest, UnknownSchedulerNameIsRejected) {
  extensions::FactoryContextImpl context(dispatcher_);
  config::ExtensionConfig config;
  config.set_name("nonexistent");

  auto result = extensions::CreateScheduler(&config, context);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
  EXPECT_NE(result.status().message().find("nonexistent"), std::string::npos);
}

TEST_F(SchedulerFactoryTest, CreatesRegisteredSchedulerFromConfig) {
  extensions::FactoryContextImpl context(dispatcher_);
  config::ExtensionConfig config;
  config.set_name("round_robin");
  config.mutable_typed_config()->PackFrom(
      extensions::schedulers::round_robin::RoundRobinSchedulerConfig());

  auto result = extensions::CreateScheduler(&config, context);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ((*result)->RequiredProtocol(), "push");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::extensions::schedulers
