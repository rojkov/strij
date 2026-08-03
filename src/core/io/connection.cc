#include "core/io/connection.hh"

#include <unistd.h>

#include <cassert>

namespace strij::io {

Connection::Connection(int connection_fd, event::DispatcherSharedPtr dispatcher,
                       event::CommandHandler* owner, ConnectionFactory factory)
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
    if (res > 0) {
      write_offset_ += static_cast<size_t>(res);
      if (write_offset_ < write_buf_.size()) {
        dispatcher_->PrepareWrite(
            this, kWrite, fd_,
            std::span<const std::byte>(
                std::next(write_buf_.data(), static_cast<ssize_t>(write_offset_)),
                write_buf_.size() - write_offset_),
            0);
      } else {
        write_buf_.clear();
        write_offset_ = 0;
      }
    } else {
      write_buf_.clear();
      write_offset_ = 0;
    }
  }
}

void Connection::Write(std::span<const std::byte> data) {
  assert(write_buf_.empty() && "previous write still in-flight");
  write_buf_.assign(data.begin(), data.end());
  write_offset_ = 0;
  dispatcher_->PrepareWrite(this, kWrite, fd_,
                            std::span<const std::byte>(write_buf_.data(), write_buf_.size()), 0);
}

void Connection::onEndOfStream() {
  ::close(fd_);
  dispatcher_->SubmitCommand(
      {.type_ = event::Command::CLOSE_CONNECTION, .destination_ = owner_, .args_ = this});
}

} // namespace strij::io
