#pragma once

#include <expected>
#include <string_view>

#include "openworkflow/document.hh"
#include "openworkflow/parser/errors.hh"

namespace strij::openworkflow {

// Parses Open Workflow DSL text into a Document. Never throws: all failures are
// reported as a single positioned ParseError.
auto Parse(std::string_view text) -> std::expected<Document, parser::ParseError>;

} // namespace strij::openworkflow
