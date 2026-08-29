#include "extensions/schedulers/scheduler.hh"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "core/config/extensions.pb.h"
#include "core/extensions/extension_registry.hh"
#include "google/protobuf/any.pb.h"

namespace strij::extensions {

namespace {

template <typename FactoryT, typename ContextT>
auto createSchedulerFromExtension(const config::ExtensionConfig& ext, ContextT& context)
    -> absl::StatusOr<SchedulerPtr> {
  auto& registry = Registry<FactoryT>::instance();
  auto* factory = registry.GetFactory(ext.name());
  if (factory == nullptr) {
    const auto names = registry.GetRegisteredNames();
    return absl::NotFoundError(
        absl::StrCat("Scheduler '", ext.name(),
                     "' is not registered. Registered: ", absl::StrJoin(names, ", ")));
  }

  auto config_msg = factory->CreateEmptyConfigProto();
  // Tolerate a scheduler without a packed typed_config (e.g. push and
  // round_robin ship no serialized payload): the factory defaults apply.
  if (!ext.typed_config().type_url().empty()) {
    ::google::protobuf::Any unpacked;
    unpacked.CopyFrom(ext.typed_config());
    if (!unpacked.UnpackTo(config_msg.get())) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to unpack typed_config for scheduler '", ext.name(),
                       "': unknown type '", unpacked.type_url(), "'"));
    }
  }

  return factory->Create(*config_msg, context);
}

} // namespace

auto CreateGatewayScheduler(const config::ExtensionConfig& config, GatewayFactoryContext& context)
    -> absl::StatusOr<SchedulerPtr> {
  return createSchedulerFromExtension<GatewaySchedulerFactory>(config, context);
}

auto CreateNodeScheduler(const config::ExtensionConfig& config, NodeagentFactoryContext& context)
    -> absl::StatusOr<SchedulerPtr> {
  return createSchedulerFromExtension<NodeSchedulerFactory>(config, context);
}

} // namespace strij::extensions