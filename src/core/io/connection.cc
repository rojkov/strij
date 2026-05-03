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
    dispatcher_->PrepareRead(this, fd_, buffer_, 0);
  }
}

} // namespace carrot::io
