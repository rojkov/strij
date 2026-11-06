#pragma once

#include "absl/status/statusor.h"
#include "gateway/config/gateway.pb.h"
#include "strij/gateway/event_plane.hh"

namespace strij::loaders {

// Resolves GatewayConfig.event_plane through Registry<EventPlaneFactory>,
// unpacks its typed config, and constructs the extension.
auto CreateEventPlane(const config::EventPlaneConfig& config,
                      extensions::GatewayFactoryContext& context,
                      const gateway::EventPlaneDeps& deps)
    -> absl::StatusOr<gateway::EventPlanePtr>;

} // namespace strij::loaders
