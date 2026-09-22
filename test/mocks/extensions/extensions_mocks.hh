#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "gmock/gmock.h"
#include "strij/extensions/data_dependency_fetcher.hh"
#include "strij/extensions/factory_context.hh"
#include "strij/nodeagent/task_handlers.hh"

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
              (const ::google::protobuf::Message& config, const nodeagent::TaskHandlerDeps& deps),
              (override));
};

class MockDataDependencyFetcher final : public DataDependencyFetcher {
public:
  MOCK_METHOD(void, Fetch,
              (const strij::task::DataRef& ref, const std::string& task_id,
               event::Dispatcher& dispatcher, event::CommandHandler* destination),
              (override));
  MOCK_METHOD(std::span<const std::string_view>, HandledSourceTypes, (), (const, override));
};

class MockDataDependencyFetcherFactory final : public nodeagent::DataDependencyFetcherFactory {
public:
  MOCK_METHOD(std::string, Name, (), (const, override));
  MOCK_METHOD(nodeagent::DataDependencyFetcherFactory::MessagePtr, CreateEmptyConfigProto, (),
              (override));
  MOCK_METHOD(extensions::DataDependencyFetcherPtr, Create,
              (const ::google::protobuf::Message& config,
               const nodeagent::DataDependencyFetcherDeps& deps),
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

} // namespace strij::extensions