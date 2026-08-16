#pragma once

#include <string>

#include "absl/status/statusor.h"
#include "core/node/capabilities.pb.h"
#include "extensions/task_handlers/task_handlers.hh"

namespace strij::extensions::task_handlers {

class EchoTaskHandler final : public TaskHandler {
public:
  void HandleTask(const strij::task::Task& task, ResultSenderPtr sender) override;
};

class EchoTaskHandlerFactory final : public TaskHandlerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> TaskHandlerPtr override;
  auto ParseConfig(const ::google::protobuf::Message& config)
      -> absl::StatusOr<strij::node::HandlerCapacity> override;
};

} // namespace strij::extensions::task_handlers
