#pragma once

#include "absl/status/statusor.h"
#include "common/config/extensions.pb.h"
#include "strij/extensions/factory_context.hh"
#include "strij/extensions/scheduler.hh"

namespace strij::loaders {

// Loads a gateway-side scheduler from a scheduler ExtensionConfig: looks up the
// named factory in extensions::Registry<gateway::GatewaySchedulerFactory>,
// unpacks (or tolerates the absence of) its typed_config, and creates the
// instance. NotFoundError when the name is not registered. The side's registry
// is selected by the context argument's type.
auto CreateScheduler(const config::ExtensionConfig& config,
                     extensions::GatewayFactoryContext& context)
    -> absl::StatusOr<extensions::SchedulerPtr>;

// Loads a nodeagent-side scheduler from a scheduler ExtensionConfig: looks up
// the named factory in extensions::Registry<nodeagent::NodeSchedulerFactory>,
// unpacks (or tolerates the absence of) its typed_config, and creates the
// instance. NotFoundError when the name is not registered. The side's registry
// is selected by the deps argument's type.
auto CreateScheduler(const config::ExtensionConfig& config,
                     const nodeagent::NodeSchedulerDeps& deps)
    -> absl::StatusOr<extensions::SchedulerPtr>;

} // namespace strij::loaders
