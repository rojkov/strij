#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "google/protobuf/repeated_ptr_field.h"

#include "absl/status/statusor.h"
#include "core/config/extensions.pb.h"
#include "core/extensions/factory_context.hh"
#include "extensions/task_handlers/task_handlers.hh"

namespace strij::nodeagent {

class TaskHandlerManager {
public:
  TaskHandlerManager() = default;

  auto GetHandler(const std::string& type) const -> strij::extensions::TaskHandler*;
  void AddHandler(std::string type, std::unique_ptr<strij::extensions::TaskHandler> handler);
  void RemoveHandler(const std::string& type);
  bool empty() const;

private:
  std::unordered_map<std::string, std::unique_ptr<strij::extensions::TaskHandler>> handlers_;
};

auto BuildTaskHandlerManager(
    const ::google::protobuf::RepeatedPtrField<strij::config::ExtensionConfig>& configs,
    strij::extensions::FactoryContext& context)
    -> absl::StatusOr<std::shared_ptr<TaskHandlerManager>>;

} // namespace strij::nodeagent
