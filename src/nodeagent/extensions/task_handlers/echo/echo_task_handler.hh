#pragma once

#include <string>

#include "absl/status/statusor.h"
#include "common/node/capabilities.pb.h"
#include "strij/nodeagent/task_handlers.hh"

namespace strij::nodeagent::task_handlers {

class EchoTaskHandler final : public TaskHandler {
public:
  void HandleTask(const task::Task& task, ResultSenderPtr sender) override;
};

class EchoTaskHandlerFactory final : public TaskHandlerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, const TaskHandlerDeps& deps)
      -> TaskHandlerPtr override;
  auto ParseConfig(const ::google::protobuf::Message& config)
      -> absl::StatusOr<node::HandlerCapacity> override;
};

} // namespace strij::nodeagent::task_handlers
