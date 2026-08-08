#include "extensions/task_handlers/echo/echo_task_handler.hh"

#include <string>

#include "core/task/task.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/mocks/extensions/extensions_mocks.hh"

namespace strij::extensions::task_handlers {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(EchoTaskHandlerTest, DeliversTaskResultWithMatchingIdBodyAndFinalFlag) {
  strij::task::Task task;
  task.set_id("42");
  task.set_body("hello");

  auto sender = std::make_unique<MockResultSender>();
  strij::task::TaskResult sent;
  EXPECT_CALL(*sender, Send(::testing::_)).WillOnce(::testing::SaveArg<0>(&sent));

  EchoTaskHandler handler;
  handler.HandleTask(task, std::move(sender));

  EXPECT_EQ(sent.id(), "42");
  EXPECT_EQ(sent.body(), "hello");
  EXPECT_TRUE(sent.is_final());
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::extensions::task_handlers
