#include "nodeagent_factory_context.hh"

#include <memory>
#include <utility>

#include "core/extensions/factory_context.hh"
#include "core/extensions/function_resolver.hh"
#include "core/logging/logger.hh"
#include "core/nodeagent/admission_controller.hh"
#include "core/nodeagent/run_task_service.hh"

namespace strij::nodeagent {

NodeagentFactoryContextImpl::NodeagentFactoryContextImpl(
    event::DispatcherSharedPtr dispatcher, extensions::FunctionResolverPtr function_resolver,
    AdmissionControllerSharedPtr admission)
    : dispatcher_{std::move(dispatcher)}, logger_{logging::Logger::GetInstance()},
      function_resolver_{std::move(function_resolver)}, admission_{std::move(admission)} {}

auto NodeagentFactoryContextImpl::Dispatcher() -> event::Dispatcher& { return *dispatcher_; }

auto NodeagentFactoryContextImpl::Logger() -> logging::Logger& { return logger_; }

auto NodeagentFactoryContextImpl::FunctionResolver() -> extensions::FunctionResolver& {
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