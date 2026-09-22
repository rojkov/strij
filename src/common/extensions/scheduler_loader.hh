#pragma once

#include "absl/status/statusor.h"
#include "common/config/extensions.pb.h"
#include "strij/extensions/factory_context.hh"
#include "strij/extensions/scheduler.hh"

namespace strij::gateway {

// Loads a gateway-side scheduler from a scheduler ExtensionConfig: looks up the
// named factory in extensions::Registry<gateway::GatewaySchedulerFactory>,
// unpacks (or tolerates the absence of) its typed_config, and creates the
// instance. NotFoundError when the name is not registered.
auto CreateGatewayScheduler(const config::ExtensionConfig& config,
                            extensions::GatewayFactoryContext& context)
    -> absl::StatusOr<extensions::SchedulerPtr>;

} // namespace strij::gateway

namespace strij::nodeagent {

// Loads a nodeagent-side scheduler from a scheduler ExtensionConfig: looks up
// the named factory in extensions::Registry<nodeagent::NodeSchedulerFactory>,
// unpacks (or tolerates the absence of) its typed_config, and creates the
// instance. NotFoundError when the name is not registered.
auto CreateNodeScheduler(const config::ExtensionConfig& config,
                         const nodeagent::NodeSchedulerDeps& deps)
    -> absl::StatusOr<extensions::SchedulerPtr>;

} // namespace strij::nodeagent