#include <memory>
#include <string>
#include <utility>

#include "test/mocks/event/mocks.hh"
#include "test/mocks/extensions/extensions_mocks.hh"
#include "test/mocks/extensions/nodeagent_deps.hh"

#include "common/config/extensions.pb.h"
#include "gmock/gmock.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "gtest/gtest.h"
#include "nodeagent/core/function_resolver.hh"
#include "nodeagent/core/task_handler_manager.hh"
#include "nodeagent/extensions/task_handlers/echo/echo_task_handler.pb.h"
#include "nodeagent/extensions/task_handlers/piped_executable/piped_executable.pb.h"
#include "strij/extensions/extension_registry.hh"

namespace strij::nodeagent {
namespace {

using ::testing::_;
using ::testing::Return;

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(TaskHandlerManagerTest, GetHandlerReturnsRegisteredHandler) {
  TaskHandlerManager manager;
  auto handler = std::make_unique<extensions::MockTaskHandler>();
  auto* raw = handler.get();
  manager.AddHandler("echo", std::move(handler));

  EXPECT_EQ(manager.GetHandler("echo"), raw);
  EXPECT_EQ(manager.GetHandler("unknown"), nullptr);
  EXPECT_FALSE(manager.empty());
}

TEST(TaskHandlerManagerTest, AddHandlerOverwritesSameType) {
  TaskHandlerManager manager;
  auto first = std::make_unique<extensions::MockTaskHandler>();
  auto second = std::make_unique<extensions::MockTaskHandler>();
  auto* raw = second.get();

  manager.AddHandler("echo", std::move(first));
  manager.AddHandler("echo", std::move(second));

  EXPECT_EQ(manager.GetHandler("echo"), raw);
}

class TaskHandlerManagerLoaderTest : public ::testing::Test {
protected:
  event::MockDispatcher dispatcher_;
  LocalFunctionResolver resolver_;
  extensions::StubChildTaskSubmitter submitter_;
  TaskHandlerDeps deps_{dispatcher_, resolver_, submitter_};
};

TEST_F(TaskHandlerManagerLoaderTest, EmptyListBuildsEmptyManager) {
  TaskHandlerManager manager;
  ::google::protobuf::RepeatedPtrField<config::ExtensionConfig> configs;

  auto status = manager.LoadTaskHandlers(configs, deps_);

  ASSERT_TRUE(status.ok());
  EXPECT_TRUE(manager.empty());
}

TEST_F(TaskHandlerManagerLoaderTest, UnknownHandlerNameReturnsError) {
  TaskHandlerManager manager;
  ::google::protobuf::RepeatedPtrField<config::ExtensionConfig> configs;
  auto* ext = configs.Add();
  ext->set_name("no_such_handler");

  auto status = manager.LoadTaskHandlers(configs, deps_);

  ASSERT_FALSE(status.ok());
  EXPECT_NE(status.message().find("no_such_handler"), std::string::npos);
}

TEST_F(TaskHandlerManagerLoaderTest, BuildInstantiatesHandlerFromConfig) {
  auto factory = std::make_unique<extensions::MockTaskHandlerFactory>();
  EXPECT_CALL(*factory, Name()).WillRepeatedly(Return("mock"));
  EXPECT_CALL(*factory, CreateEmptyConfigProto())
      .WillOnce(Return(std::make_unique<extensions::task_handlers::echo::EchoTaskHandlerConfig>()));
  EXPECT_CALL(*factory, Create(_, _))
      .WillOnce(Return(std::make_unique<extensions::MockTaskHandler>()));
  // The singleton registry owns the factory for the program lifetime.
  ::testing::Mock::AllowLeak(factory.get());
  extensions::Registry<nodeagent::TaskHandlerFactory>::instance().RegisterFactory(
      "mock", factory.release());

  TaskHandlerManager manager;
  ::google::protobuf::RepeatedPtrField<config::ExtensionConfig> configs;
  auto* ext = configs.Add();
  ext->set_name("mock");
  extensions::task_handlers::echo::EchoTaskHandlerConfig typed;
  ext->mutable_typed_config()->PackFrom(typed);

  auto status = manager.LoadTaskHandlers(configs, deps_);

  ASSERT_TRUE(status.ok());
  EXPECT_FALSE(manager.empty());
  EXPECT_NE(manager.GetHandler("mock"), nullptr);
}

TEST_F(TaskHandlerManagerLoaderTest, BuildInstantiatesPipedExecutableHandlerFromConfig) {
  TaskHandlerManager manager;
  ::google::protobuf::RepeatedPtrField<config::ExtensionConfig> configs;
  auto* ext = configs.Add();
  ext->set_name("piped_executable");
  extensions::task_handlers::piped_executable::PipedExecutableTaskHandlerConfig typed;
  ext->mutable_typed_config()->PackFrom(typed);

  auto status = manager.LoadTaskHandlers(configs, deps_);

  ASSERT_TRUE(status.ok());
  EXPECT_FALSE(manager.empty());
  EXPECT_NE(manager.GetHandler("piped_executable"), nullptr);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent
