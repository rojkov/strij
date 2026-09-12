#pragma once

#include "strij/event/dispatcher.hh"
#include "strij/extensions/factory_context.hh"
#include "strij/nodeagent/admission_controller.hh"
#include "strij/nodeagent/function_resolver.hh"
#include "strij/nodeagent/object_cache.hh"
#include "strij/nodeagent/run_task_service.hh"

namespace strij::nodeagent {

// Concrete NodeagentFactoryContext used by the nodeagent binary. The RunTask
// service, the data-dependency fetch router, and the child submitter are
// installed after construction (two-phase) because they depend on objects
// built around this context (the task handler manager, the fetchers, and the
// scheduler router). The child forwarder (GatewayClient) is dependency-free
// and is therefore provided directly at construction.
class NodeagentFactoryContextImpl final : public extensions::NodeagentFactoryContext {
public:
  NodeagentFactoryContextImpl(event::DispatcherSharedPtr dispatcher,
                              nodeagent::FunctionResolverPtr function_resolver,
                              AdmissionControllerSharedPtr admission,
                              ObjectCacheSharedPtr object_cache,
                              nodeagent::ChildTaskForwarder& child_task_forwarder);

  auto Dispatcher() -> event::Dispatcher& override;
  auto SharedDispatcher() -> event::DispatcherSharedPtr override;

  auto FunctionResolver() -> nodeagent::FunctionResolver& override;
  auto AdmissionController() -> AdmissionControllerSharedPtr override;
  auto RunTaskService() -> nodeagent::RunTaskService& override;
  auto ObjectCache() -> nodeagent::ObjectCache& override;
  auto DataDependencyFetcherRouter() -> nodeagent::DataDependencyFetcherRouter& override;
  auto ChildTaskForwarder() -> nodeagent::ChildTaskForwarder& override;
  auto ChildTaskSubmitter() -> nodeagent::ChildTaskSubmitter& override;

  void SetRunTaskService(nodeagent::RunTaskService& run_task_service);
  void SetDataDependencyFetcherRouter(
      nodeagent::DataDependencyFetcherRouter& data_dependency_fetcher_router);
  void SetChildTaskSubmitter(nodeagent::ChildTaskSubmitter& child_task_submitter);

private:
  event::DispatcherSharedPtr dispatcher_;
  nodeagent::FunctionResolverPtr function_resolver_;
  AdmissionControllerSharedPtr admission_;
  ObjectCacheSharedPtr object_cache_;
  nodeagent::RunTaskService* run_task_service_{nullptr};
  nodeagent::DataDependencyFetcherRouter* data_dependency_fetcher_router_{nullptr};
  nodeagent::ChildTaskForwarder* child_task_forwarder_;
  nodeagent::ChildTaskSubmitter* child_task_submitter_{nullptr};
};

} // namespace strij::nodeagent