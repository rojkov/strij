#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "common/config/extensions.pb.h"
#include "common/extensions/evaluators/evaluator.hh"

namespace strij::loaders {

// Loads an evaluator from an evaluator ExtensionConfig (spec
// expression-evaluation): resolves the named factory in
// extensions::Registry<EvaluatorFactory>, unpacks (or tolerates the absence
// of) its typed_config, and compiles `source` with the declared variables.
// NotFoundError when the name is not registered. Mirrors
// CreateGatewayScheduler/CreateNodeScheduler (scheduler_loader) and is shared
// by gateway and nodeagent consumers; deliberate config->instance glue, hence
// its own loaders/ layer.
auto CreateEvaluator(const config::ExtensionConfig& config,
                     const extensions::evaluators::EvaluatorDeps& deps, std::string_view source,
                     std::vector<std::string> variable_names)
    -> absl::StatusOr<extensions::evaluators::EvaluatorPtr>;

} // namespace strij::loaders