#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "strij/extensions/factory_context.hh"
#include "nodeagent/extensions/task_handlers/task_handlers.hh"
#include "gmock/gmock.h"

namespace strij::extensions {

class MockResultSender final : public nodeagent::ResultSender {
public:
  MOCK_METHOD(void, Send, (strij::task::TaskResult result), (override));
  MOCK_METHOD(std::size_t, RegisterOnClose, (std::move_only_function<void()> cb), (override));
  MOCK_METHOD(void, UnregisterOnClose, (std::size_t token), (override));
};

class MockTaskHandler final : public nodeagent::TaskHandler {
public:
  MOCK_METHOD(void, HandleTask, (const strij::task::Task& task, nodeagent::ResultSenderPtr sender),
              (override));
};

class MockTaskHandlerFactory final : public nodeagent::TaskHandlerFactory {
public:
  MOCK_METHOD(std::string, Name, (), (const, override));
  MOCK_METHOD(MessagePtr, CreateEmptyConfigProto, (), (override));
  MOCK_METHOD(nodeagent::TaskHandlerPtr, Create,
              (const ::google::protobuf::Message& config, NodeagentFactoryContext& context),
              (override));
};

// Gateway-side factory context mock. Tests typically only exercise the two
// gateway-specific accessors; the base Dispatcher() is uninteresting unless a
// factory under test actually uses it.
class MockGatewayFactoryContext final : public GatewayFactoryContext {
public:
  MOCK_METHOD(event::Dispatcher&, Dispatcher, (), (override));
  MOCK_METHOD(event::DispatcherSharedPtr, SharedDispatcher, (), (override));
  MOCK_METHOD(gateway::NodeDirectory&, NodeDirectory, (), (override));
  MOCK_METHOD(gateway::ResultReceiverStorage&, ResultReceiverStorage, (), (override));
};

// Nodeagent-side factory context mock. Task handler and node scheduler
// factories reach FunctionResolver/AdmissionController/RunTaskService through
// it; only the methods a factory actually calls are expected in tests.
class MockNodeagentFactoryContext final : public NodeagentFactoryContext {
public:
  MOCK_METHOD(event::Dispatcher&, Dispatcher, (), (override));
  MOCK_METHOD(event::DispatcherSharedPtr, SharedDispatcher, (), (override));
  MOCK_METHOD(strij::nodeagent::FunctionResolver&, FunctionResolver, (), (override));
  MOCK_METHOD(nodeagent::AdmissionControllerSharedPtr, AdmissionController, (), (override));
  MOCK_METHOD(nodeagent::RunTaskService&, RunTaskService, (), (override));
};

} // namespace strij::extensions