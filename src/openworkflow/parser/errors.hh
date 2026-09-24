#pragma once

#include <cstddef>
#include <string>

#include "yaml-cpp/yaml.h"

namespace strij::openworkflow::parser {

// A structural parse failure. Carries the byte position into the original input
// text where the failure was detected plus an already-formatted message.
class ParseError : public YAML::Exception {
public:
  ParseError(const YAML::Mark& mark, const std::string& message)
      : YAML::Exception(mark, message),
        position_(mark.pos < 0 ? 0 : static_cast<std::size_t>(mark.pos)) {}

  [[nodiscard]] auto Position() const -> std::size_t { return position_; }
  [[nodiscard]] auto Message() const -> const std::string& { return msg; }

private:
  std::size_t position_;
};

} // namespace strij::openworkflow::parser
