#pragma once

#include <memory>

#include "core/extensions/factory_context.hh"
#include "core/gateway/node_directory.hh"
#include "core/gateway/result_receiver_storage.hh"
#include "core/logging/logger.hh"
#include "strij/event/dispatcher.hh"

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
  auto Logger() -> logging::Logger& override;

  auto NodeDirectory() -> gateway::NodeDirectory& override;
  auto ResultReceiverStorage() -> gateway::ResultReceiverStorage& override;

private:
  event::DispatcherSharedPtr dispatcher_;
  logging::Logger& logger_;
  gateway::NodeDirectory& node_directory_;
  gateway::ResultReceiverStorage& storage_;
};

} // namespace strij::gateway