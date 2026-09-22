#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "absl/status/status.h"
#include "common/config/extensions.pb.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "strij/nodeagent/task_handlers.hh"

namespace strij::nodeagent {

class TaskHandlerManager {
public:
  TaskHandlerManager() = default;

  auto GetHandler(const std::string& type) const -> nodeagent::TaskHandler*;
  // Registers `handler` for `type`, replacing any existing handler. Public only
  // for test setup: production populates the manager through LoadTaskHandlers,
  // which calls this internally. It could be made private once the test call
  // sites are migrated to LoadTaskHandlers.
  void AddHandler(std::string type, nodeagent::TaskHandlerPtr handler);
  [[nodiscard]] auto empty() const -> bool;

  // Instantiates the configured task handlers into this manager. The caller
  // creates the manager empty before the run-task service is built over it (the
  // service only reads it at task execution time), then calls this to populate
  // it after the child scheduler router exists. Returns InvalidArgumentError
  // when an entry names an unregistered factory or its typed_config does not
  // unpack. An empty list is valid: a warning is logged and the manager is left
  // unchanged.
  auto
  LoadTaskHandlers(const ::google::protobuf::RepeatedPtrField<config::ExtensionConfig>& configs,
                   const TaskHandlerDeps& deps) -> absl::Status;

private:
  std::unordered_map<std::string, nodeagent::TaskHandlerPtr> handlers_;
};

using TaskHandlerManagerSharedPtr = std::shared_ptr<TaskHandlerManager>;

} // namespace strij::nodeagent
