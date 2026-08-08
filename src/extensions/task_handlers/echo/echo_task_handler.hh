#pragma once

#include <memory>
#include <string>

#include "extensions/task_handlers/task_handlers.hh"

namespace strij::extensions::task_handlers {

class EchoTaskHandler final : public TaskHandler {
public:
  void HandleTask(const strij::task::Task& task, std::unique_ptr<ResultSender> sender) override;
};

class EchoTaskHandlerFactory final : public TaskHandlerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> std::unique_ptr<TaskHandler> override;
};

} // namespace strij::extensions::task_handlers
