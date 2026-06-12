#include "core/io/connection.hh"

#include <format>

#include "src/core/io/llhttp_parser.hh"

#include "core/logging/log.hh"

namespace carrot::io {

Connection::Connection(int connection_fd, event::DispatcherSharedPtr dispatcher)
    : fd_{connection_fd}, dispatcher_{std::move(dispatcher)},
      parser_{std::make_unique<LlhttpParser>(
          [this](event::IOObject* reader, std::span<std::byte> buf) -> void {
            dispatcher_->PrepareRead(reader, fd_, buf, 0);
          },
          std::bind(&Connection::onEndOfStream, this),
          [this](std::span<const std::byte> buf) {
            response_ = std::format(
                "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: "
                "text/plain\r\nConnection: close\r\n\r\n{}",
                buf.size(), std::string{reinterpret_cast<const char*>(buf.data()), buf.size()});

            auto response_bytes = std::as_bytes(std::span(response_.data(), response_.size()));
            dispatcher_->PrepareWrite(nullptr, fd_, response_bytes, 0);
          })} {}

void Connection::onEndOfStream() {
  // TODO: close and delete the connection
}

} // namespace carrot::io
