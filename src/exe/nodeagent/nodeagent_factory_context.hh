#pragma once

#include "core/extensions/factory_context.hh"
#include "core/extensions/function_resolver.hh"
#include "core/logging/logger.hh"
#include "core/nodeagent/admission_controller.hh"
#include "core/nodeagent/run_task_service.hh"
#include "strij/event/dispatcher.hh"

namespace strij::nodeagent {

// Concrete NodeagentFactoryContext used by the nodeagent binary. The RunTask
// service is installed after construction (two-phase) because it depends on the
// task handler manager and admission controller, which are built around this
// context.
class NodeagentFactoryContextImpl final : public extensions::NodeagentFactoryContext {
public:
  NodeagentFactoryContextImpl(event::DispatcherSharedPtr dispatcher,
                              extensions::FunctionResolverPtr function_resolver,
                              AdmissionControllerSharedPtr admission);

  auto Dispatcher() -> event::Dispatcher& override;
  auto Logger() -> logging::Logger& override;

  auto FunctionResolver() -> extensions::FunctionResolver& override;
  auto AdmissionController() -> AdmissionControllerSharedPtr override;
  auto RunTaskService() -> nodeagent::RunTaskService& override;

  void SetRunTaskService(nodeagent::RunTaskService& run_task_service);

private:
  event::DispatcherSharedPtr dispatcher_;
  logging::Logger& logger_;
  extensions::FunctionResolverPtr function_resolver_;
  AdmissionControllerSharedPtr admission_;
  nodeagent::RunTaskService* run_task_service_{nullptr};
};

} // namespace strij::nodeagent