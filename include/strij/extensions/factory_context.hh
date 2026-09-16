#pragma once

#include <memory>

#include "strij/common/pure.hh"
#include "strij/event/dispatcher.hh"

namespace strij::gateway {

class NodeDirectory;
class ResultReceiverStorage;

} // namespace strij::gateway

namespace strij::nodeagent {

class FunctionResolver;
class AdmissionController;
class RunTaskService;
class ObjectCache;
class DataDependencyFetcherRouter;
class ChildTaskSubmitter;
class ChildSubmissionService;
using AdmissionControllerSharedPtr = std::shared_ptr<AdmissionController>;

} // namespace strij::nodeagent

namespace strij::extensions {

// Base services shared by every extension factory category. Factory contexts
// are constructed once per process and handed to extension factories at Create;
// extensions must not retain the context past their owner's lifetime.
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

// Nodeagent-side extension services. Only nodeagent scheduler extensions and
// task handler extensions are created with this context.
class NodeagentFactoryContext : public FactoryContext {
public:
  virtual auto FunctionResolver() -> nodeagent::FunctionResolver& PURE;
  virtual auto AdmissionController() -> nodeagent::AdmissionControllerSharedPtr PURE;
  virtual auto RunTaskService() -> nodeagent::RunTaskService& PURE;
  virtual auto ObjectCache() -> nodeagent::ObjectCache& PURE;
  // The process-global data dependency fetch router, built at startup from
  // NodeAgentConfig.data_dependency_fetchers. Schedulers use it to prefetch
  // Task.deps and to gate task readiness on dep availability.
  virtual auto DataDependencyFetcherRouter() -> nodeagent::DataDependencyFetcherRouter& PURE;
  // The shared child-policy step (admit-or-run-or-forward), used by the local
  // schedulers' Schedule facet. Installed before schedulers are created.
  virtual auto ChildSubmissionService() -> nodeagent::ChildSubmissionService& PURE;
  // The node-global child submission handle wrapping the composite
  // ChildSchedulerRouter. Installed after the local schedulers are built.
  virtual auto ChildTaskSubmitter() -> nodeagent::ChildTaskSubmitter& PURE;
};

} // namespace strij::extensions