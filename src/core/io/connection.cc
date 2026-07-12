#include "core/io/connection.hh"

#include <unistd.h>

#include <format>
#include <string_view>

#include "src/core/io/llhttp_parser.hh"

namespace carrot::io {

Connection::Connection(int connection_fd, event::DispatcherSharedPtr dispatcher,
                       event::IOObject* owner)
    : fd_{connection_fd}, dispatcher_{std::move(dispatcher)}, owner_{owner},
      parser_{std::make_unique<LlhttpParser>(
          [this](event::IOObject* reader, std::span<std::byte> buf) -> void {
            dispatcher_->PrepareRead(reader, 0, fd_, buf, 0);
          },
          [this]() -> void { onEndOfStream(); },
          [this](std::span<const std::byte> buf) -> void {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            auto body = std::string_view(reinterpret_cast<const char*>(buf.data()), buf.size());
            response_ = std::format("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: "
                                    "text/plain\r\nConnection: close\r\n\r\n{}",
                                    buf.size(), body);

            auto response_bytes = std::as_bytes(std::span(response_.data(), response_.size()));
            dispatcher_->PrepareWrite(parser_.get(), static_cast<uint8_t>(LlhttpParser::Op::Write),
                                      fd_, response_bytes, 0);
          })} {}

void Connection::onEndOfStream() {
  ::close(fd_);
  dispatcher_->SubmitCommand(
      {.type_ = event::Command::CLOSE_CONNECTION, .destination_ = owner_, .args_ = this});
}

} // namespace carrot::io
