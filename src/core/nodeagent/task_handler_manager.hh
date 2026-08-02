#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "google/protobuf/repeated_ptr_field.h"

#include "absl/status/statusor.h"
#include "core/config/extensions.pb.h"
#include "core/extensions/factory_context.hh"
#include "extensions/task_handlers/task_handlers.hh"

namespace carrot::nodeagent {

class TaskHandlerManager {
public:
  TaskHandlerManager() = default;

  auto GetHandler(const std::string& type) const -> carrot::extensions::TaskHandler*;
  void AddHandler(std::string type, std::unique_ptr<carrot::extensions::TaskHandler> handler);
  void RemoveHandler(const std::string& type);
  bool empty() const;

private:
  std::unordered_map<std::string, std::unique_ptr<carrot::extensions::TaskHandler>> handlers_;
};

auto BuildTaskHandlerManager(
    const ::google::protobuf::RepeatedPtrField<carrot::config::ExtensionConfig>& configs,
    carrot::extensions::FactoryContext& context)
    -> absl::StatusOr<std::shared_ptr<TaskHandlerManager>>;

} // namespace carrot::nodeagent
