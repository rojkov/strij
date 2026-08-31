#include "nodeagent/extensions/task_handlers/echo/echo_task_handler.hh"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/extensions/extension_registry.hh"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/task_handlers/echo/echo_task_handler.pb.h"

namespace strij::nodeagent::task_handlers {

void EchoTaskHandler::HandleTask(const task::Task& task, ResultSenderPtr sender) {
  task::TaskResult result;
  result.set_id(task.id());
  result.set_body(task.body());
  result.set_is_final(true);
  sender->Send(std::move(result));
}

auto EchoTaskHandlerFactory::Name() const -> std::string { return "echo"; }

auto EchoTaskHandlerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<extensions::task_handlers::echo::EchoTaskHandlerConfig>();
}

auto EchoTaskHandlerFactory::Create(const ::google::protobuf::Message& /*config*/,
                                    extensions::NodeagentFactoryContext& /*context*/) -> TaskHandlerPtr {
  return std::make_unique<EchoTaskHandler>();
}

auto EchoTaskHandlerFactory::ParseConfig(const ::google::protobuf::Message& config)
    -> absl::StatusOr<node::HandlerCapacity> {
  const auto* echo_config = dynamic_cast<const extensions::task_handlers::echo::EchoTaskHandlerConfig*>(&config);
  if (echo_config == nullptr) {
    return absl::InvalidArgumentError("config is not an EchoTaskHandlerConfig");
  }
  return echo_config->capacity();
}

} // namespace strij::nodeagent::task_handlers

REGISTER_FACTORY_FULLY_QUALIFIED(strij::nodeagent::task_handlers::EchoTaskHandlerFactory,
                                 strij::nodeagent::TaskHandlerFactory, echo_task_handler_registrar)
