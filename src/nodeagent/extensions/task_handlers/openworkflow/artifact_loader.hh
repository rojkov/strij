#pragma once

#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace strij::nodeagent::task_handlers::openworkflow {

// Loads a workflow document from a local filesystem artifact. The load is
// synchronous by design (see AGENTS.md): the artifact is expected to be small
// and the load completes before any interpreter state exists. Only regular
// files are accepted, and the size is capped so the event-loop thread cannot be
// blocked by a special file or an unbounded read.
auto LoadWorkflowArtifact(std::string_view path) -> absl::StatusOr<std::string>;

} // namespace strij::nodeagent::task_handlers::openworkflow
