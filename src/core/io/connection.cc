#include "core/io/connection.hh"

#include <unistd.h>

#include <cassert>

#include "core/logging/log.hh"

namespace strij::io {

Connection::Connection(int connection_fd, event::DispatcherSharedPtr dispatcher,
                       event::CommandHandler* owner, const ConnectionFactory& factory)
    : fd_{connection_fd}, dispatcher_{std::move(dispatcher)}, owner_{owner},
      mailbox_{std::make_shared<OutboundMailbox>(*this)} {
  parser_ = factory(*this);
  dispatcher_->PrepareRead(this, kRead, fd_, parser_->GetReadBuffer(), 0);
}

Connection::~Connection() { mailbox_->Close(); }

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
      auto& front = write_queue_.front();
      if (write_offset_ < front.size()) {
        dispatcher_->PrepareWrite(
            this, kWrite, fd_,
            std::span<const std::byte>(std::next(front.data(), static_cast<ssize_t>(write_offset_)),
                                       front.size() - write_offset_),
            0);
      } else {
        write_queue_.pop_front();
        write_offset_ = 0;
        if (!write_queue_.empty()) {
          dispatcher_->PrepareWrite(
              this, kWrite, fd_,
              std::span<const std::byte>(write_queue_.front().data(), write_queue_.front().size()),
              0);
        }
      }
    } else {
      LOG_WARNING("Connection write failed (res={}), dropping {} queued writes", res,
                  write_queue_.size());
      write_queue_.clear();
      write_offset_ = 0;
    }
  }
}

void Connection::Write(std::span<const std::byte> data) {
  if (fd_ < 0) {
    return;
  }
  write_queue_.emplace_back(data.begin(), data.end());
  if (write_queue_.size() == 1) {
    dispatcher_->PrepareWrite(
        this, kWrite, fd_,
        std::span<const std::byte>(write_queue_.front().data(), write_queue_.front().size()), 0);
  }
}

auto Connection::Mailbox() -> std::shared_ptr<OutboundMailbox> { return mailbox_; }

void Connection::Close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }

  mailbox_->Close();
}

void Connection::onEndOfStream() {
  ::close(fd_);
  fd_ = -1;
  mailbox_->Close();
  dispatcher_->SubmitCommand(
      {.type_ = event::Command::DEFERRED_DELETE, .destination_ = owner_, .args_ = this});
}

} // namespace strij::io
