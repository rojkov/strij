#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "absl/status/statusor.h"
#include "common/config/extensions.pb.h"
#include "strij/extensions/factory_context.hh"
#include "nodeagent/extensions/task_handlers/task_handlers.hh"
#include "google/protobuf/repeated_ptr_field.h"

namespace strij::nodeagent {

class TaskHandlerManager {
public:
  TaskHandlerManager() = default;

  auto GetHandler(const std::string& type) const -> nodeagent::TaskHandler*;
  void AddHandler(std::string type, nodeagent::TaskHandlerPtr handler);
  void RemoveHandler(const std::string& type);
  [[nodiscard]] auto empty() const -> bool;

private:
  std::unordered_map<std::string, nodeagent::TaskHandlerPtr> handlers_;
};

using TaskHandlerManagerSharedPtr = std::shared_ptr<TaskHandlerManager>;

auto BuildTaskHandlerManager(
    const ::google::protobuf::RepeatedPtrField<config::ExtensionConfig>& configs,
    extensions::NodeagentFactoryContext& context) -> absl::StatusOr<TaskHandlerManagerSharedPtr>;

} // namespace strij::nodeagent
