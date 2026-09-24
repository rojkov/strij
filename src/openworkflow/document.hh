#pragma once

#include <optional>
#include <string>
#include <variant>

#include "openworkflow/events.hh"
#include "openworkflow/task.hh"
#include "openworkflow/types.hh"
#include "openworkflow/use.hh"
#include "openworkflow/value.hh"

namespace strij::openworkflow {

// The workflow's identity and descriptive metadata.
struct DocumentInfo {
    std::string dsl_;
    std::string namespace_;
    std::string name_;
    std::string version_;
    std::optional<std::string> title_;
    std::optional<std::string> summary_;
    Value tags_;
    Value metadata_;
};

// Schedules the workflow.
struct Schedule {
    std::optional<Duration> every_;
    std::optional<std::string> cron_;
    std::optional<Duration> after_;
    std::optional<EventConsumptionStrategy> on_;
    std::optional<std::string> read_;
};

// Configures the workflow's runtime expression evaluation.
struct Evaluate {
    std::optional<std::string> language_;
    std::optional<std::string> mode_;
};

// A complete Open Workflow DSL document.
struct Document {
    DocumentInfo document_;
    std::optional<Input> input_;
    std::optional<Use> use_;
    Tasks do_;
    std::optional<std::variant<Timeout, std::string>> timeout_;
    std::optional<Output> output_;
    std::optional<Schedule> schedule_;
    std::optional<Evaluate> evaluate_;
};

} // namespace strij::openworkflow
