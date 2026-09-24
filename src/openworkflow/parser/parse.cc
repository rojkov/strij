#include "openworkflow/parser/parse.hh"

#include <expected>
#include <string>
#include <string_view>

#include "openworkflow/document.hh"
#include "openworkflow/parser/decode.hh"
#include "openworkflow/parser/errors.hh"
#include "yaml-cpp/yaml.h"

namespace strij::openworkflow {

auto Parse(std::string_view text) -> std::expected<Document, parser::ParseError> {
  try {
    const YAML::Node root = YAML::Load(std::string(text));
    return root.as<Document>();
  } catch (const parser::ParseError& error) {
    return std::unexpected(error);
  } catch (const YAML::Exception& error) {
    return std::unexpected(parser::ParseError(error.mark, error.msg));
  }
}

} // namespace strij::openworkflow
