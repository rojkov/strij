#include "common/loaders/event_plane_loader.hh"

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "strij/extensions/extension_registry.hh"
#include "strij/gateway/event_plane.hh"

namespace strij::loaders {

auto CreateEventPlane(const config::EventPlaneConfig& config,
                      extensions::GatewayFactoryContext& context,
                      const gateway::EventPlaneDeps& deps)
    -> absl::StatusOr<gateway::EventPlanePtr> {
  if (!config.has_extension() || config.extension().name().empty()) {
    return absl::InvalidArgumentError("event_plane.extension.name is required");
  }

  auto* factory = extensions::Registry<gateway::EventPlaneFactory>::instance().GetFactory(
      config.extension().name());
  if (factory == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("event-plane extension '", config.extension().name(),
                     "' not found; ensure the extension library is linked"));
  }

  ::google::protobuf::Any unpacked;
  unpacked.CopyFrom(config.extension().typed_config());
  auto config_msg = factory->CreateEmptyConfigProto();
  if (!unpacked.UnpackTo(config_msg.get())) {
    return absl::InvalidArgumentError(
        absl::StrCat("failed to unpack typed_config for event-plane extension '",
                     config.extension().name(), "': unknown type '", unpacked.type_url(), "'"));
  }

  return factory->Create(*config_msg, context, deps);
}

} // namespace strij::loaders
