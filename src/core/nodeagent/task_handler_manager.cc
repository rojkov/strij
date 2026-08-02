#include "core/nodeagent/task_handler_manager.hh"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "core/extensions/extension_registry.hh"
#include "core/logging/log.hh"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"

namespace carrot::nodeagent {

auto TaskHandlerManager::GetHandler(const std::string& type) const
    -> carrot::extensions::TaskHandler* {
  auto it = handlers_.find(type);
  return it != handlers_.end() ? it->second.get() : nullptr;
}

void TaskHandlerManager::AddHandler(std::string type,
                                    std::unique_ptr<carrot::extensions::TaskHandler> handler) {
  handlers_.insert_or_assign(std::move(type), std::move(handler));
}

void TaskHandlerManager::RemoveHandler(const std::string& type) { handlers_.erase(type); }

bool TaskHandlerManager::empty() const { return handlers_.empty(); }

auto BuildTaskHandlerManager(
    const ::google::protobuf::RepeatedPtrField<carrot::config::ExtensionConfig>& configs,
    carrot::extensions::FactoryContext& context)
    -> absl::StatusOr<std::shared_ptr<TaskHandlerManager>> {
  auto manager = std::make_shared<TaskHandlerManager>();

  if (configs.empty()) {
    LOG_WARNING("No task handlers configured; all tasks will be dropped");
    return manager;
  }

  auto& registry = carrot::extensions::Registry<carrot::extensions::TaskHandlerFactory>::instance();
  for (const auto& ext : configs) {
    auto* factory = registry.GetFactory(ext.name());
    if (factory == nullptr) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Task handler '", ext.name(), "' not found. Ensure the extension library is linked "
                                        "and the name matches a registered factory."));
    }

    ::google::protobuf::Any unpacked;
    unpacked.CopyFrom(ext.typed_config());
    auto config_msg = factory->CreateEmptyConfigProto();
    if (!unpacked.UnpackTo(config_msg.get())) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Failed to unpack typed_config for task handler '", ext.name(),
          "': unknown type '", unpacked.type_url(), "'"));
    }

    auto handler = factory->Create(*config_msg, context);
    manager->AddHandler(factory->Name(), std::move(handler));
    LOG_INFO("Task handler '{}' loaded", ext.name());
  }

  return manager;
}

} // namespace carrot::nodeagent
