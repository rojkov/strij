#pragma once

#include <memory>
#include <string>

#include "extensions/task_handlers/task_handlers.hh"

namespace carrot::extensions::task_handlers {

class EchoTaskHandler final : public TaskHandler {
public:
  void HandleTask(const carrot::task::Task& task, ResultSender& sender) override;
};

class EchoTaskHandlerFactory final : public TaskHandlerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> std::unique_ptr<TaskHandler> override;
};

} // namespace carrot::extensions::task_handlers
