#include "core/io/connection.hh"

#include "core/logging/log.hh"

namespace carrot::io {

Connection::Connection(int connection_fd, event::DispatcherSharedPtr dispatcher)
    : fd_{connection_fd}, dispatcher_{std::move(dispatcher)} {
  dispatcher_->PrepareRead(this, fd_, buffer_, 0);
}

void Connection::HandleCompletion(int res, uint32_t flags) {
  LOG_DEBUG("Got read {} bytes. Flags: {}", res, flags);
  if (res != 0) {
    // TODO: decode the buffer and send decoded messages to the session object
    dispatcher_->PrepareRead(this, fd_, buffer_, 0);
    dispatcher_->PrepareWrite(this, fd_, std::span(buffer_).subspan(0, res), 0);
  } else {
    // TODO: close and delete the connection
  }
}

} // namespace carrot::io
