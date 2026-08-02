#include "extensions/task_handlers/echo/echo_task_handler.hh"

#include <memory>
#include <string>
#include <utility>

#include "core/task/task.pb.h"
#include "extensions/task_handlers/echo/echo_task_handler.pb.h"

namespace carrot::extensions::task_handlers {

void EchoTaskHandler::HandleTask(const carrot::task::Task& task, ResultSender& sender) {
  carrot::task::TaskResult result;
  result.set_id(task.id());
  result.set_body(task.body());
  result.set_is_final(true);
  sender.Send(std::move(result));
}

auto EchoTaskHandlerFactory::Name() const -> std::string { return "echo"; }

auto EchoTaskHandlerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<carrot::extensions::task_handlers::echo::EchoTaskHandlerConfig>();
}

auto EchoTaskHandlerFactory::Create(const ::google::protobuf::Message& /*config*/,
                                    FactoryContext& /*context*/)
    -> std::unique_ptr<TaskHandler> {
  return std::make_unique<EchoTaskHandler>();
}

} // namespace carrot::extensions::task_handlers

REGISTER_FACTORY_FULLY_QUALIFIED(carrot::extensions::task_handlers::EchoTaskHandlerFactory,
                                 carrot::extensions::TaskHandlerFactory,
                                 echo_task_handler_registrar)
