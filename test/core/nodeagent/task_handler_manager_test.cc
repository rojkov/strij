#include "core/nodeagent/task_handler_manager.hh"

#include <memory>
#include <string>
#include <utility>

#include "core/config/extensions.pb.h"
#include "core/extensions/function_resolver.hh"
#include "extensions/task_handlers/echo/echo_task_handler.pb.h"
#include "extensions/task_handlers/piped_executable/piped_executable.pb.h"
#include "gmock/gmock.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "gtest/gtest.h"
#include "test/mocks/event/mocks.hh"
#include "test/mocks/extensions/extensions_mocks.hh"

namespace strij::nodeagent {
namespace {

using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::_;

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(TaskHandlerManagerTest, GetHandlerReturnsRegisteredHandler) {
  TaskHandlerManager manager;
  auto handler = std::make_unique<strij::extensions::MockTaskHandler>();
  auto* raw = handler.get();
  manager.AddHandler("echo", std::move(handler));

  EXPECT_EQ(manager.GetHandler("echo"), raw);
  EXPECT_EQ(manager.GetHandler("unknown"), nullptr);
  EXPECT_FALSE(manager.empty());
}

TEST(TaskHandlerManagerTest, AddHandlerOverwritesSameType) {
  TaskHandlerManager manager;
  auto first = std::make_unique<strij::extensions::MockTaskHandler>();
  auto second = std::make_unique<strij::extensions::MockTaskHandler>();
  auto* raw = second.get();

  manager.AddHandler("echo", std::move(first));
  manager.AddHandler("echo", std::move(second));

  EXPECT_EQ(manager.GetHandler("echo"), raw);
}

TEST(TaskHandlerManagerTest, RemoveHandlerErasesType) {
  TaskHandlerManager manager;
  manager.AddHandler("echo", std::make_unique<strij::extensions::MockTaskHandler>());

  manager.RemoveHandler("echo");

  EXPECT_EQ(manager.GetHandler("echo"), nullptr);
  EXPECT_TRUE(manager.empty());
}

TEST(TaskHandlerManagerTest, EmptyListBuildsEmptyManager) {
  strij::extensions::MockFactoryContext context;
  ::google::protobuf::RepeatedPtrField<strij::config::ExtensionConfig> configs;

  auto result = BuildTaskHandlerManager(configs, context);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE((*result)->empty());
}

TEST(TaskHandlerManagerTest, UnknownHandlerNameReturnsError) {
  strij::extensions::MockFactoryContext context;
  ::google::protobuf::RepeatedPtrField<strij::config::ExtensionConfig> configs;
  auto* ext = configs.Add();
  ext->set_name("no_such_handler");

  auto result = BuildTaskHandlerManager(configs, context);

  ASSERT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("no_such_handler"), std::string::npos);
}

TEST(TaskHandlerManagerTest, BuildInstantiatesHandlerFromConfig) {
  auto factory = std::make_unique<strij::extensions::MockTaskHandlerFactory>();
  EXPECT_CALL(*factory, Name()).WillRepeatedly(Return("mock"));
  EXPECT_CALL(*factory, CreateEmptyConfigProto())
      .WillOnce(Return(std::make_unique<strij::extensions::task_handlers::echo::EchoTaskHandlerConfig>()));
  EXPECT_CALL(*factory, Create(_, _))
      .WillOnce(Return(std::make_unique<strij::extensions::MockTaskHandler>()));
  // The singleton registry owns the factory for the program lifetime.
  ::testing::Mock::AllowLeak(factory.get());
  strij::extensions::Registry<strij::extensions::TaskHandlerFactory>::instance().RegisterFactory(
      "mock", factory.release());

  strij::extensions::MockFactoryContext context;
  ::google::protobuf::RepeatedPtrField<strij::config::ExtensionConfig> configs;
  auto* ext = configs.Add();
  ext->set_name("mock");
  strij::extensions::task_handlers::echo::EchoTaskHandlerConfig typed;
  ext->mutable_typed_config()->PackFrom(typed);

  auto result = BuildTaskHandlerManager(configs, context);

  ASSERT_TRUE(result.ok());
  EXPECT_FALSE((*result)->empty());
  EXPECT_NE((*result)->GetHandler("mock"), nullptr);
}

TEST(TaskHandlerManagerTest, BuildInstantiatesPipedExecutableHandlerFromConfig) {
  strij::extensions::MockFactoryContext context;
  strij::extensions::LocalFunctionResolver resolver;
  event::MockDispatcher dispatcher;
  ON_CALL(context, Dispatcher()).WillByDefault(ReturnRef(dispatcher));
  ON_CALL(context, FunctionResolver()).WillByDefault(ReturnRef(resolver));
  ::google::protobuf::RepeatedPtrField<strij::config::ExtensionConfig> configs;
  auto* ext = configs.Add();
  ext->set_name("piped_executable");
  strij::extensions::task_handlers::piped_executable::PipedExecutableTaskHandlerConfig typed;
  ext->mutable_typed_config()->PackFrom(typed);

  auto result = BuildTaskHandlerManager(configs, context);

  ASSERT_TRUE(result.ok());
  EXPECT_FALSE((*result)->empty());
  EXPECT_NE((*result)->GetHandler("piped_executable"), nullptr);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent
