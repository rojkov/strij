#include "gateway_factory_context.hh"

#include <utility>

#include "common/extensions/factory_context.hh"
#include "gateway/core/node_directory.hh"
#include "gateway/core/result_receiver_storage.hh"
#include "common/core/logging/logger.hh"

namespace strij::gateway {

GatewayFactoryContextImpl::GatewayFactoryContextImpl(event::DispatcherSharedPtr dispatcher,
                                                     gateway::NodeDirectory& node_directory,
                                                     gateway::ResultReceiverStorage& storage)
    : dispatcher_{std::move(dispatcher)}, logger_{logging::Logger::GetInstance()},
      node_directory_{node_directory}, storage_{storage} {}

auto GatewayFactoryContextImpl::Dispatcher() -> event::Dispatcher& { return *dispatcher_; }

auto GatewayFactoryContextImpl::Logger() -> logging::Logger& { return logger_; }

auto GatewayFactoryContextImpl::NodeDirectory() -> gateway::NodeDirectory& {
  return node_directory_;
}

auto GatewayFactoryContextImpl::ResultReceiverStorage() -> gateway::ResultReceiverStorage& {
  return storage_;
}

} // namespace strij::gateway