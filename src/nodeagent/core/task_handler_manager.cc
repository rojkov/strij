#include "nodeagent/core/task_handler_manager.hh"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/core/logging/log.hh"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "strij/extensions/extension_registry.hh"

namespace strij::nodeagent {

auto TaskHandlerManager::GetHandler(const std::string& type) const -> nodeagent::TaskHandler* {
  auto iter = handlers_.find(type);
  return iter != handlers_.end() ? iter->second.get() : nullptr;
}

void TaskHandlerManager::AddHandler(std::string type, nodeagent::TaskHandlerPtr handler) {
  handlers_.insert_or_assign(std::move(type), std::move(handler));
}

auto TaskHandlerManager::empty() const -> bool { return handlers_.empty(); }

auto TaskHandlerManager::LoadTaskHandlers(
    const ::google::protobuf::RepeatedPtrField<config::ExtensionConfig>& configs,
    const TaskHandlerDeps& deps) -> absl::Status {
  if (configs.empty()) {
    LOG_WARNING("No task handlers configured; all tasks will be dropped");
    return absl::OkStatus();
  }

  auto& registry = extensions::Registry<nodeagent::TaskHandlerFactory>::instance();
  for (const auto& ext : configs) {
    auto* factory = registry.GetFactory(ext.name());
    if (factory == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("Task handler '", ext.name(),
                       "' not found. Ensure the extension library is linked "
                       "and the name matches a registered factory."));
    }

    ::google::protobuf::Any unpacked;
    unpacked.CopyFrom(ext.typed_config());
    auto config_msg = factory->CreateEmptyConfigProto();
    if (!unpacked.UnpackTo(config_msg.get())) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to unpack typed_config for task handler '", ext.name(),
                       "': unknown type '", unpacked.type_url(), "'"));
    }

    auto handler = factory->Create(*config_msg, deps);
    AddHandler(factory->Name(), std::move(handler));
    LOG_INFO("Task handler '{}' loaded", ext.name());
  }

  return absl::OkStatus();
}

} // namespace strij::nodeagent
