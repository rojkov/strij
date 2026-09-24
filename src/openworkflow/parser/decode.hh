#pragma once

#include "openworkflow/document.hh"
#include "yaml-cpp/yaml.h"

namespace strij::openworkflow::parser {

// Decodes an already-loaded YAML node into a Document. Throws ParseError on any
// structural violation.
auto DecodeDocument(const YAML::Node& node, Document& out) -> bool;

} // namespace strij::openworkflow::parser

namespace YAML {

template <> struct convert<strij::openworkflow::Document> {
  static auto decode(const Node& node, strij::openworkflow::Document& rhs) -> bool;
};

} // namespace YAML
