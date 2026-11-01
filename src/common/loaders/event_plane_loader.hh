#pragma once

#include "absl/status/statusor.h"
#include "gateway/config/gateway.pb.h"
#include "strij/extensions/event_plane.hh"

namespace strij::loaders {

// Resolves GatewayConfig.event_plane through Registry<EventPlaneFactory>,
// unpacks its typed config, and constructs the extension.
auto CreateEventPlane(const config::EventPlaneConfig& config,
                      extensions::GatewayFactoryContext& context,
                      const extensions::EventPlaneDeps& deps)
    -> absl::StatusOr<extensions::EventPlanePtr>;

} // namespace strij::loaders
