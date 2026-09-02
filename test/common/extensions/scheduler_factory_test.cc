#include <memory>
#include <string>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"
#include "test/mocks/extensions/extensions_mocks.hh"

#include "absl/status/status.h"
#include "common/config/extensions.pb.h"
#include "gateway/core/node_directory.hh"
#include "gateway/core/result_receiver_storage.hh"
#include "common/core/io/protocol_parser.hh"
#include "gateway/extensions/schedulers/round_robin/round_robin.pb.h"
#include "strij/extensions/scheduler.hh"
#include "common/extensions/scheduler_loader.hh"
#include "gtest/gtest.h"

namespace strij::gateway::schedulers {
namespace {

using ::testing::Return;
using ::testing::ReturnRef;

class SchedulerFactoryTest : public ::testing::Test {
protected:
  std::shared_ptr<event::MockDispatcher> dispatcher_{std::make_shared<event::MockDispatcher>()};
  gateway::ResultReceiverStorageImpl storage_;
  gateway::NodeDirectoryImpl directory_{
      dispatcher_,
      [](io::Connection&) -> io::ProtocolParserPtr {
        return std::make_unique<io::TrivialParser>();
      },
      storage_};
  extensions::MockGatewayFactoryContext context_;

  void SetUp() override {
    ON_CALL(context_, NodeDirectory()).WillByDefault(ReturnRef(directory_));
    ON_CALL(context_, ResultReceiverStorage()).WillByDefault(ReturnRef(storage_));
  }
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(SchedulerFactoryTest, EmptySchedulerNameIsRejected) {
  config::ExtensionConfig config;

  auto result = CreateGatewayScheduler(config, context_);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
  EXPECT_NE(result.status().message().find("Scheduler"), std::string::npos);
}

TEST_F(SchedulerFactoryTest, UnknownSchedulerNameIsRejected) {
  config::ExtensionConfig config;
  config.set_name("nonexistent");

  auto result = CreateGatewayScheduler(config, context_);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
  EXPECT_NE(result.status().message().find("nonexistent"), std::string::npos);
}

TEST_F(SchedulerFactoryTest, CreatesRegisteredSchedulerFromConfig) {
  config::ExtensionConfig config;
  config.set_name("round_robin");
  config.mutable_typed_config()->PackFrom(
      extensions::schedulers::round_robin::RoundRobinSchedulerConfig());

  auto result = CreateGatewayScheduler(config, context_);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ((*result)->RequiredProtocol(), "push");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::gateway::schedulers