#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "absl/status/statusor.h"
#include "core/config/extensions.pb.h"
#include "core/extensions/factory_context.hh"
#include "extensions/task_handlers/task_handlers.hh"
#include "google/protobuf/repeated_ptr_field.h"

namespace strij::nodeagent {

class TaskHandlerManager {
public:
  TaskHandlerManager() = default;

  auto GetHandler(const std::string& type) const -> extensions::TaskHandler*;
  void AddHandler(std::string type, extensions::TaskHandlerPtr handler);
  void RemoveHandler(const std::string& type);
  [[nodiscard]] auto empty() const -> bool;

private:
  std::unordered_map<std::string, extensions::TaskHandlerPtr> handlers_;
};

using TaskHandlerManagerSharedPtr = std::shared_ptr<TaskHandlerManager>;

auto BuildTaskHandlerManager(
    const ::google::protobuf::RepeatedPtrField<config::ExtensionConfig>& configs,
    extensions::FactoryContext& context) -> absl::StatusOr<TaskHandlerManagerSharedPtr>;

} // namespace strij::nodeagent
