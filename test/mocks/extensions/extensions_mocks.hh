#pragma once

#include <cstddef>
#include <functional>
#include <memory>
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
  MOCK_METHOD(void, HandleTask, (const strij::task::Task& task, ResultSender& sender), (override));
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

} // namespace strij::extensions
