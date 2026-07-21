#include "core/io/connection.hh"

#include <unistd.h>

namespace carrot::io {

Connection::Connection(int connection_fd, event::DispatcherSharedPtr dispatcher,
                       event::IOObject* owner, ConnectionFactory factory)
    : fd_{connection_fd}, dispatcher_{std::move(dispatcher)}, owner_{owner} {
  parser_ = factory(*this);
  dispatcher_->PrepareRead(this, kRead, fd_, parser_->GetReadBuffer(), 0);
}

void Connection::HandleCompletion(uint8_t tag, int res, uint32_t /*flags*/) {
  if (tag == kRead) {
    if (res > 0) {
      auto action = parser_->OnData(static_cast<size_t>(res));
      if (action == ProtocolParser::Action::NeedMoreData) {
        dispatcher_->PrepareRead(this, kRead, fd_, parser_->GetReadBuffer(), 0);
      }
    } else {
      onEndOfStream();
    }
  } else if (tag == kWrite) {
  }
}

void Connection::Write(std::span<const std::byte> data) {
  write_buf_.assign(data.begin(), data.end());
  dispatcher_->PrepareWrite(this, kWrite, fd_,
                            std::as_bytes(std::span(write_buf_.data(), write_buf_.size())), 0);
}

void Connection::onEndOfStream() {
  ::close(fd_);
  dispatcher_->SubmitCommand(
      {.type_ = event::Command::CLOSE_CONNECTION, .destination_ = owner_, .args_ = this});
}

} // namespace carrot::io
