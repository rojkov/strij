#include "core/utils/errors.hh"

#include <system_error>

namespace strij::utils {

auto GetErrorString(int err_code) -> const char* {
  const std::error_code error_code{err_code, std::generic_category()};
  const std::system_error system_error{error_code, "connect"};
  return system_error.what();
}

} // namespace strij::utils