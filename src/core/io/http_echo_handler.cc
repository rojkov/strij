#include "core/io/http_echo_handler.hh"

#include <format>
#include <string>
#include <string_view>

#include "core/io/connection.hh"

namespace carrot::io {

void HttpEchoHandler::OnMessage(std::span<const std::byte> msg, Connection& conn) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto body = std::string_view(reinterpret_cast<const char*>(msg.data()), msg.size());
  auto response = std::format("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: "
                              "text/plain\r\nConnection: close\r\n\r\n{}",
                              msg.size(), body);
  auto response_bytes = std::as_bytes(std::span(response.data(), response.size()));
  conn.Write(response_bytes);
}

} // namespace carrot::io
