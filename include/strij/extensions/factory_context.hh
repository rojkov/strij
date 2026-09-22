#pragma once

#include "strij/common/pure.hh"
#include "strij/event/dispatcher.hh"

namespace strij::gateway {

class NodeDirectory;
class ResultReceiverStorage;

} // namespace strij::gateway

namespace strij::extensions {

// Base services shared by every gateway extension factory category. Factory
// contexts are constructed once per process and handed to extension factories
// at Create; extensions must not retain the context past their owner's
// lifetime.
//
// The nodeagent side does not use a shared factory context: each nodeagent
// extension category receives its own narrow dependency bundle
// (TaskHandlerDeps, NodeSchedulerDeps, DataDependencyFetcherDeps).
class FactoryContext {
public:
  FactoryContext() = default;
  virtual ~FactoryContext() = default;

  FactoryContext(const FactoryContext&) = delete;
  auto operator=(const FactoryContext&) -> FactoryContext& = delete;
  FactoryContext(FactoryContext&&) noexcept = delete;
  auto operator=(FactoryContext&&) noexcept -> FactoryContext& = delete;

  virtual auto Dispatcher() -> event::Dispatcher& PURE;

  // Owning handle to the event loop, for services that must retain it past the
  // factory call (e.g. a PeriodicTimer sweep). The concrete context impls own
  // the Dispatcher as a shared_ptr, so returning it is cheap.
  virtual auto SharedDispatcher() -> event::DispatcherSharedPtr PURE;
};

// Gateway-side extension services. Only gateway scheduler extensions and node
// discovery extensions are created with this context.
class GatewayFactoryContext : public FactoryContext {
public:
  virtual auto NodeDirectory() -> gateway::NodeDirectory& PURE;
  virtual auto ResultReceiverStorage() -> gateway::ResultReceiverStorage& PURE;
};

} // namespace strij::extensions
