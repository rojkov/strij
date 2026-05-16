#include "core/io/connection.hh"

#include <format>

#include "src/core/io/llhttp_parser.hh"

#include "core/logging/log.hh"

namespace carrot::io {

Reader::Reader(std::function<void(std::span<std::byte>)>&& on_read_completion)
    : on_read_completion_{std::move(on_read_completion)} {}

void Reader::HandleCompletion(int res, uint32_t flags) {
  LOG_DEBUG("Got read {} bytes. Flags: {}", res, flags);
  on_read_completion_({buffer_.data(), static_cast<size_t>(res)});
}

Connection::Connection(int connection_fd, event::DispatcherSharedPtr dispatcher)
    : fd_{connection_fd}, dispatcher_{std::move(dispatcher)},
      reader_{std::bind(&Connection::onReadCompletion, this, std::placeholders::_1)},
      parser_{std::make_unique<LlhttpParser>([this](std::span<const std::byte> buf) {
        response_ = std::format("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: "
                                "text/plain\r\nConnection: close\r\n\r\n{}",
                                buf.size(),
                                std::string{reinterpret_cast<const char*>(buf.data()), buf.size()});

        auto response_bytes = std::as_bytes(std::span(response_.data(), response_.size()));
        dispatcher_->PrepareWrite(nullptr, fd_, response_bytes, 0);
      })} {
  dispatcher_->PrepareRead(&reader_, fd_, parser_->ReadBuffer(), 0);
}

void Connection::onReadCompletion(std::span<std::byte> data) {
  if (data.empty()) {
    // TODO: close and delete the connection
    return;
  }

  parser_->Parse(data.size());

  // TODO: decode the buffer and send decoded messages to the session object
  dispatcher_->PrepareRead(&reader_, fd_, parser_->ReadBuffer(), 0);
}

} // namespace carrot::io
