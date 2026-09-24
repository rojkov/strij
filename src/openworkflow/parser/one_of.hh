#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

#include "openworkflow/parser/errors.hh"
#include "yaml-cpp/yaml.h"

namespace strij::openworkflow::parser {

using KeySet = std::initializer_list<std::string_view>;

// Selects exactly one present key out of `keys`. Throws a positioned
// ParseError when zero or more than one key is defined on `node`.
inline auto SelectOne(const YAML::Node& node, KeySet keys, std::string_view what)
    -> std::string_view {
  std::string_view selected;
  std::string_view first_seen;
  std::string_view second_seen;
  std::size_t count = 0;
  for (const std::string_view key : keys) {
    if (!node.IsMap()) {
      break;
    }
    const YAML::Node child = node[std::string(key)];
    if (child.IsDefined() && !child.IsNull()) {
      if (count == 0) {
        selected = key;
        first_seen = key;
      } else if (count == 1) {
        second_seen = key;
      }
      ++count;
    }
  }

  if (count == 0) {
    std::string expected;
    for (const std::string_view key : keys) {
      if (!expected.empty()) {
        expected += ", ";
      }
      expected += "'" + std::string(key) + "'";
    }
    throw ParseError(node.Mark(), "expected exactly one " + std::string(what) +
                                      " key but found none among " + expected);
  }
  if (count > 1) {
    throw ParseError(node.Mark(), "keys '" + std::string(first_seen) + "' and '" +
                                      std::string(second_seen) + "' are mutually exclusive for " +
                                      std::string(what));
  }
  return selected;
}

} // namespace strij::openworkflow::parser
