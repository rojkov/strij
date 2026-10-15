#pragma once

#include "common/core/utils/jv.hh"
#include "openworkflow/value.hh"

namespace strij::openworkflow {

// The only bridge between the DSL's JSON-faithful `Value` tree and the libjq
// `Jv` boundary. `Value` has no JSON representation of its own (it is a
// yaml-free model), so expressions run against it through this pair.
//
// `ValueToJv` never fails: `monostate` becomes JSON null. Integers keep their
// exact decimal literal (so `INT64_MIN`/`INT64_MAX` survive a round trip) while
// doubles stay native, which lets `JvToValue` tell `1` from `1.0`.
auto ValueToJv(const Value& value) -> utils::Jv;

// Converts a `Jv` back into a `Value`. A number carrying an integer literal
// becomes `int64_t`; every other number becomes `double`. An invalid `Jv` maps
// to `monostate`.
auto JvToValue(const utils::Jv& value) -> Value;

} // namespace strij::openworkflow
