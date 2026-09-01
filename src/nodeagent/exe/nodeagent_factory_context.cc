#include "nodeagent_factory_context.hh"

#include <memory>
#include <utility>

#include "common/extensions/factory_context.hh"
#include "nodeagent/core/function_resolver.hh"
#include "common/core/logging/logger.hh"
#include "nodeagent/core/admission_controller.hh"
#include "nodeagent/core/run_task_service.hh"

namespace strij::nodeagent {

NodeagentFactoryContextImpl::NodeagentFactoryContextImpl(
    event::DispatcherSharedPtr dispatcher, nodeagent::FunctionResolverPtr function_resolver,
    AdmissionControllerSharedPtr admission)
    : dispatcher_{std::move(dispatcher)}, function_resolver_{std::move(function_resolver)},
      admission_{std::move(admission)} {}

auto NodeagentFactoryContextImpl::Dispatcher() -> event::Dispatcher& { return *dispatcher_; }

auto NodeagentFactoryContextImpl::FunctionResolver() -> nodeagent::FunctionResolver& {
  return *function_resolver_;
}

auto NodeagentFactoryContextImpl::AdmissionController() -> AdmissionControllerSharedPtr {
  return admission_;
}

auto NodeagentFactoryContextImpl::RunTaskService() -> nodeagent::RunTaskService& {
  return *run_task_service_;
}

void NodeagentFactoryContextImpl::SetRunTaskService(nodeagent::RunTaskService& run_task_service) {
  run_task_service_ = &run_task_service;
}

} // namespace strij::nodeagent