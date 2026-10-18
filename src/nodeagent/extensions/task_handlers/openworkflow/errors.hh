#pragma once

#include <cstdint>
#include <string>

#include "common/task/task.pb.h"

namespace strij::nodeagent::task_handlers::openworkflow {

// Which phase produced a workflow failure. Validation facts are derivable from
// the document alone; execution failures depend on the data the run was given.
// A report can therefore state whether the document is malformed or the run was
// given the wrong data.
enum class Phase : std::uint8_t { kValidation = 0, kExecution = 1 };

// A workflow failure plus the classification used to build the final
// `TaskResult`. `step_` names the failing step (empty when none applies).
struct Failure {
  Phase phase_ = Phase::kValidation;
  task::TaskStatus status_ = task::TASK_STATUS_INTERNAL;
  std::string step_;
  std::string message_;
};

} // namespace strij::nodeagent::task_handlers::openworkflow
