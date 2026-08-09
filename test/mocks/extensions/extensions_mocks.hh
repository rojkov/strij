#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "core/extensions/factory_context.hh"
#include "extensions/task_handlers/task_handlers.hh"
#include "gmock/gmock.h"

namespace strij::extensions {

class MockResultSender final : public ResultSender {
public:
  MOCK_METHOD(void, Send, (strij::task::TaskResult result), (override));
  MOCK_METHOD(std::size_t, RegisterOnClose, (std::move_only_function<void()> cb), (override));
  MOCK_METHOD(void, UnregisterOnClose, (std::size_t token), (override));
};

class MockTaskHandler final : public TaskHandler {
public:
  MOCK_METHOD(void, HandleTask, (const strij::task::Task& task, ResultSenderPtr sender),
              (override));
};

class MockTaskHandlerFactory final : public TaskHandlerFactory {
public:
  MOCK_METHOD(std::string, Name, (), (const, override));
  MOCK_METHOD(MessagePtr, CreateEmptyConfigProto, (), (override));
  MOCK_METHOD(TaskHandlerPtr, Create,
              (const ::google::protobuf::Message& config, FactoryContext& context), (override));
};

class MockFactoryContext final : public FactoryContext {
public:
  MOCK_METHOD(event::Dispatcher&, Dispatcher, (), (override));
  MOCK_METHOD(logging::Logger&, Logger, (), (override));
  MOCK_METHOD(::strij::extensions::FunctionResolver&, FunctionResolver, (), (override));
};

} // namespace strij::extensions
