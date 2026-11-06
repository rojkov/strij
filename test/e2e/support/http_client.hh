#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"

namespace strij::e2e {

struct HttpResponse {
  int status_code{0};
  std::string body;
};

/// Sends one `POST` over a fresh loopback connection, reads the response to EOF,
/// decodes `Content-Length` or `Transfer-Encoding: chunked`, and returns the
/// status code and reassembled body. This is the only way the harness observes a
/// cluster (black-box HTTP, no process internals).
auto Post(uint16_t port, std::string_view path, std::string_view body,
          const std::vector<std::pair<std::string, std::string>>& headers = {},
          std::chrono::milliseconds timeout = std::chrono::seconds{5})
    -> absl::StatusOr<HttpResponse>;

} // namespace strij::e2e
