#include "core/gateway/http_result_receiver.hh"

#include <format>
#include <string>
#include <string_view>

namespace strij::gateway {

void HttpResultReceiver::Deliver(std::span<const std::byte> value) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto body = std::string_view(reinterpret_cast<const char*>(value.data()), value.size());
  auto response = std::format("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: "
                              "text/plain\r\nConnection: close\r\n\r\n{}",
                              value.size(), body);
  auto response_bytes = std::as_bytes(std::span(response.data(), response.size()));
  conn_.Write(response_bytes);
}

} // namespace strij::gateway
