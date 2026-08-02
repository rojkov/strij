#pragma once

#include <memory>
#include <string>

#include "core/extensions/factory_context.hh"
#include "extensions/task_handlers/task_handlers.hh"
#include "gmock/gmock.h"

namespace carrot::extensions {

class MockResultSender final : public ResultSender {
public:
  MOCK_METHOD(void, Send, (carrot::task::TaskResult result), (override));
};

class MockTaskHandler final : public TaskHandler {
public:
  MOCK_METHOD(void, HandleTask, (const carrot::task::Task& task, ResultSender& sender),
              (override));
};

class MockTaskHandlerFactory final : public TaskHandlerFactory {
public:
  MOCK_METHOD(std::string, Name, (), (const, override));
  MOCK_METHOD(MessagePtr, CreateEmptyConfigProto, (), (override));
  MOCK_METHOD(std::unique_ptr<TaskHandler>, Create,
              (const ::google::protobuf::Message& config, FactoryContext& context), (override));
};

class MockFactoryContext final : public FactoryContext {
public:
  MOCK_METHOD(event::Dispatcher&, Dispatcher, (), (override));
  MOCK_METHOD(logging::Logger&, Logger, (), (override));
};

} // namespace carrot::extensions
