#include "strij/extensions/scheduler.hh"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "common/config/extensions.pb.h"
#include "common/extensions/scheduler_loader.hh"
#include "google/protobuf/any.pb.h"
#include "strij/extensions/extension_registry.hh"

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

  auto scheduler = factory->Create(*config_msg, context);
  // A factory returns nullptr to report an invalid/unsupported configuration
  // (e.g. probe candidate_count < 1); surface it as a config error rather than
  // installing a null scheduler.
  if (scheduler == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Scheduler factory '", ext.name(), "' rejected the configuration (Create returned null)"));
  }

  return scheduler;
}

} // namespace

} // namespace strij::extensions

namespace strij::gateway {

auto CreateGatewayScheduler(const config::ExtensionConfig& config,
                            extensions::GatewayFactoryContext& context)
    -> absl::StatusOr<extensions::SchedulerPtr> {
  return extensions::createSchedulerFromExtension<GatewaySchedulerFactory>(config, context);
}

} // namespace strij::gateway

namespace strij::nodeagent {

auto CreateNodeScheduler(const config::ExtensionConfig& config,
                         const nodeagent::NodeSchedulerDeps& deps)
    -> absl::StatusOr<extensions::SchedulerPtr> {
  return extensions::createSchedulerFromExtension<NodeSchedulerFactory>(config, deps);
}

} // namespace strij::nodeagent