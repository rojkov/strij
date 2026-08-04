#include "core/io/outbound_mailbox.hh"

#include <utility>

#include "core/io/connection.hh"

namespace strij::io {

OutboundMailbox::OutboundMailbox(Connection& conn) : conn_{conn} {}

void OutboundMailbox::Enqueue(std::vector<std::byte> frame) {
  if (!active_) {
    return;
  }
  conn_.Write(frame);
}

auto OutboundMailbox::RegisterOnClose(CloseCallback close_cb) -> std::size_t {
  const std::size_t token = next_token_++;
  if (!active_) {
    close_cb();
    return token;
  }
  close_callbacks_.emplace_back(token, std::move(close_cb));
  return token;
}

void OutboundMailbox::UnregisterOnClose(std::size_t token) {
  std::erase_if(close_callbacks_,
                [token](const auto& entry) -> bool { return entry.first == token; });
}

void OutboundMailbox::Close() {
  active_ = false;
  for (auto& [token, close_cb] : close_callbacks_) {
    (void)token;
    close_cb();
  }
  close_callbacks_.clear();
}

} // namespace strij::io
