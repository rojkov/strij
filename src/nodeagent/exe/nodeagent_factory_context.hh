#pragma once

#include "common/extensions/factory_context.hh"
#include "nodeagent/core/function_resolver.hh"
#include "common/core/logging/logger.hh"
#include "nodeagent/core/admission_controller.hh"
#include "nodeagent/core/run_task_service.hh"
#include "strij/event/dispatcher.hh"

namespace strij::nodeagent {

// Concrete NodeagentFactoryContext used by the nodeagent binary. The RunTask
// service is installed after construction (two-phase) because it depends on the
// task handler manager and admission controller, which are built around this
// context.
class NodeagentFactoryContextImpl final : public extensions::NodeagentFactoryContext {
public:
  NodeagentFactoryContextImpl(event::DispatcherSharedPtr dispatcher,
                              nodeagent::FunctionResolverPtr function_resolver,
                              AdmissionControllerSharedPtr admission);

  auto Dispatcher() -> event::Dispatcher& override;

  auto FunctionResolver() -> nodeagent::FunctionResolver& override;
  auto AdmissionController() -> AdmissionControllerSharedPtr override;
  auto RunTaskService() -> nodeagent::RunTaskService& override;

  void SetRunTaskService(nodeagent::RunTaskService& run_task_service);

private:
  event::DispatcherSharedPtr dispatcher_;
  nodeagent::FunctionResolverPtr function_resolver_;
  AdmissionControllerSharedPtr admission_;
  nodeagent::RunTaskService* run_task_service_{nullptr};
};

} // namespace strij::nodeagent