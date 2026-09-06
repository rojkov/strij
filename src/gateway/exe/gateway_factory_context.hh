#pragma once

#include "gateway/core/node_directory.hh"
#include "strij/event/dispatcher.hh"
#include "strij/extensions/factory_context.hh"

namespace strij::gateway {

// Concrete GatewayFactoryContext used by the gateway binary. Handed to node
// discovery and gateway scheduler factories at Create; they retain the
// references for the lifetime of the objects they build.
class GatewayFactoryContextImpl final : public extensions::GatewayFactoryContext {
public:
  GatewayFactoryContextImpl(event::DispatcherSharedPtr dispatcher,
                            gateway::NodeDirectory& node_directory,
                            gateway::ResultReceiverStorage& storage);

  auto Dispatcher() -> event::Dispatcher& override;
  auto SharedDispatcher() -> event::DispatcherSharedPtr override;

  auto NodeDirectory() -> gateway::NodeDirectory& override;
  auto ResultReceiverStorage() -> gateway::ResultReceiverStorage& override;

private:
  event::DispatcherSharedPtr dispatcher_;
  gateway::NodeDirectory& node_directory_;
  gateway::ResultReceiverStorage& storage_;
};

} // namespace strij::gateway