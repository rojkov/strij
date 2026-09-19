#include "nodeagent_factory_context.hh"

#include <memory>
#include <utility>

#include "strij/extensions/factory_context.hh"

namespace strij::nodeagent {

NodeagentFactoryContextImpl::NodeagentFactoryContextImpl(
    event::DispatcherSharedPtr dispatcher, nodeagent::FunctionResolverPtr function_resolver,
    AdmissionControllerSharedPtr admission, ObjectCacheSharedPtr object_cache,
    nodeagent::ChildTaskForwarder& child_task_forwarder)
    : dispatcher_{std::move(dispatcher)}, function_resolver_{std::move(function_resolver)},
      admission_{std::move(admission)}, object_cache_{std::move(object_cache)},
      child_task_forwarder_{&child_task_forwarder} {}

auto NodeagentFactoryContextImpl::Dispatcher() -> event::Dispatcher& { return *dispatcher_; }

auto NodeagentFactoryContextImpl::SharedDispatcher() -> event::DispatcherSharedPtr {
  return dispatcher_;
}

auto NodeagentFactoryContextImpl::FunctionResolver() -> nodeagent::FunctionResolver& {
  return *function_resolver_;
}

auto NodeagentFactoryContextImpl::AdmissionController() -> AdmissionControllerSharedPtr {
  return admission_;
}

auto NodeagentFactoryContextImpl::RunTaskService() -> nodeagent::RunTaskService& {
  return *run_task_service_;
}

auto NodeagentFactoryContextImpl::ObjectCache() -> nodeagent::ObjectCache& {
  return *object_cache_;
}

auto NodeagentFactoryContextImpl::DataDependencyFetcherRouter()
    -> nodeagent::DataDependencyFetcherRouter& {
  return *data_dependency_fetcher_router_;
}

auto NodeagentFactoryContextImpl::ChildTaskForwarder() -> nodeagent::ChildTaskForwarder& {
  return *child_task_forwarder_;
}

auto NodeagentFactoryContextImpl::ChildTaskSubmitter() -> nodeagent::ChildTaskSubmitter& {
  return *child_task_submitter_;
}

void NodeagentFactoryContextImpl::SetRunTaskService(nodeagent::RunTaskService& run_task_service) {
  run_task_service_ = &run_task_service;
}

void NodeagentFactoryContextImpl::SetDataDependencyFetcherRouter(
    nodeagent::DataDependencyFetcherRouter& data_dependency_fetcher_router) {
  data_dependency_fetcher_router_ = &data_dependency_fetcher_router;
}

void NodeagentFactoryContextImpl::SetChildTaskSubmitter(
    nodeagent::ChildTaskSubmitter& child_task_submitter) {
  child_task_submitter_ = &child_task_submitter;
}

} // namespace strij::nodeagent