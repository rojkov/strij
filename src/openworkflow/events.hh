#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "openworkflow/types.hh"
#include "openworkflow/value.hh"

namespace strij::openworkflow {

struct NamedTask;

// An ordered collection of named tasks. Declared here so the event machinery
// (subscription iterators) can reference it before task.hh completes the type.
using Tasks = std::vector<NamedTask>;

// The CloudEvents-shaped properties of an event.
struct EventProperties {
    std::optional<std::string> id_;
    std::optional<std::string> source_;
    std::optional<std::string> type_;
    std::optional<std::string> time_;
    std::optional<std::string> subject_;
    std::optional<std::string> datacontenttype_;
    std::optional<std::string> dataschema_;
    std::optional<Value> data_;
};

// A correlation maps event attributes to data attributes.
struct Correlation {
    std::string from_;
    std::optional<std::string> expect_;
};

// An event filter selects events by their properties and correlates them.
struct EventFilter {
    EventProperties with_;
    std::map<std::string, Correlation> correlate_;
};

// Describes how events are consumed: all, any (with an optional until), or one.
struct EventConsumptionStrategy {
    enum class Mode : std::uint8_t { kAll = 0, kAny = 1, kOne = 2 };

    Mode mode_ = Mode::kOne;
    std::vector<EventFilter> filters_;
    // The `any` strategy's optional until condition; kept as an opaque value
    // since it may be an expression string or a nested consumption strategy.
    std::optional<Value> until_;
};

// Iterates over each item consumed by a subscription.
struct SubscriptionIterator {
    std::optional<std::string> item_;
    std::optional<std::string> at_;
    Tasks do_;
    std::optional<Output> output_;
    std::optional<Export> export_;
};

} // namespace strij::openworkflow
