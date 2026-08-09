#include "extensions/task_handlers/echo/echo_task_handler.hh"

#include <memory>
#include <string>
#include <utility>

#include "core/task/task.pb.h"
#include "extensions/task_handlers/echo/echo_task_handler.pb.h"

namespace strij::extensions::task_handlers {

void EchoTaskHandler::HandleTask(const task::Task& task, ResultSenderPtr sender) {
  strij::task::TaskResult result;
  result.set_id(task.id());
  result.set_body(task.body());
  result.set_is_final(true);
  sender->Send(std::move(result));
}

auto EchoTaskHandlerFactory::Name() const -> std::string { return "echo"; }

auto EchoTaskHandlerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<strij::extensions::task_handlers::echo::EchoTaskHandlerConfig>();
}

auto EchoTaskHandlerFactory::Create(const ::google::protobuf::Message& /*config*/,
                                    FactoryContext& /*context*/) -> TaskHandlerPtr {
  return std::make_unique<EchoTaskHandler>();
}

} // namespace strij::extensions::task_handlers

REGISTER_FACTORY_FULLY_QUALIFIED(strij::extensions::task_handlers::EchoTaskHandlerFactory,
                                 strij::extensions::TaskHandlerFactory, echo_task_handler_registrar)
