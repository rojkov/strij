#include "gateway_factory_context.hh"

#include <utility>

#include "gateway/core/node_directory.hh"
#include "strij/extensions/factory_context.hh"

namespace strij::gateway {

GatewayFactoryContextImpl::GatewayFactoryContextImpl(event::DispatcherSharedPtr dispatcher,
                                                     gateway::NodeDirectory& node_directory,
                                                     gateway::ResultReceiverStorage& storage)
    : dispatcher_{std::move(dispatcher)}, node_directory_{node_directory}, storage_{storage} {}

auto GatewayFactoryContextImpl::Dispatcher() -> event::Dispatcher& { return *dispatcher_; }

auto GatewayFactoryContextImpl::SharedDispatcher() -> event::DispatcherSharedPtr {
  return dispatcher_;
}

auto GatewayFactoryContextImpl::NodeDirectory() -> gateway::NodeDirectory& {
  return node_directory_;
}

auto GatewayFactoryContextImpl::ResultReceiverStorage() -> gateway::ResultReceiverStorage& {
  return storage_;
}

} // namespace strij::gateway