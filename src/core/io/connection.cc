#include "core/io/connection.hh"

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
      reader_{std::bind(&Connection::onReadCompletion, this, std::placeholders::_1)} {
  dispatcher_->PrepareRead(&reader_, fd_, reader_.Buffer(), 0);
}

void Connection::onReadCompletion(std::span<std::byte> data) {
  if (data.empty()) {
    // TODO: close and delete the connection
    return;
  }

  // TODO: decode the buffer and send decoded messages to the session object
  dispatcher_->PrepareRead(&reader_, fd_, reader_.Buffer(), 0);
  dispatcher_->PrepareWrite(nullptr, fd_, reader_.Buffer(), 0);
}

} // namespace carrot::io
