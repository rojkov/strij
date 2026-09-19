#include "common/core/io/outbound_mailbox.hh"

#include <utility>

namespace strij::io {

OutboundMailbox::OutboundMailbox(WriteFn write_fn) : write_fn_{std::move(write_fn)} {}

void OutboundMailbox::Enqueue(std::vector<std::byte> frame) {
  if (!active_) {
    return;
  }

  write_fn_(std::move(frame));
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
