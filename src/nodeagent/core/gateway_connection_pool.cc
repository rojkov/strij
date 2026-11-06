#include "nodeagent/core/gateway_connection_pool.hh"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "common/core/io/outbound_mailbox.hh"

namespace strij::nodeagent {

void GatewayConnectionPool::Register(std::shared_ptr<io::OutboundMailbox> mailbox) {
  auto entry = std::make_shared<Entry>();
  entry->mailbox_ = std::move(mailbox);
  entries_.push_back(entry);

  // Capture weakly: the mailbox owns this callback and the entry owns the
  // mailbox, so a strong capture would form a cycle that leaks closed entries.
  std::weak_ptr<Entry> weak_entry = entry;
  entry->mailbox_->RegisterOnClose([weak_entry]() {
    if (auto entry = weak_entry.lock()) {
      entry->live_ = false;
    }
  });
}

auto GatewayConnectionPool::LiveCount() const -> std::size_t {
  return std::count_if(entries_.begin(), entries_.end(),
                       [](const std::shared_ptr<Entry>& entry) { return entry->live_; });
}

void GatewayConnectionPool::prune() {
  std::erase_if(entries_, [](const std::shared_ptr<Entry>& entry) { return !entry->live_; });
}

auto GatewayConnectionPool::Next() -> io::OutboundMailbox* {
  prune();

  if (entries_.empty()) {
    return nullptr;
  }

  for (std::size_t offset = 0; offset < entries_.size(); ++offset) {
    const std::size_t index = (next_index_ + offset) % entries_.size();
    if (entries_[index]->live_) {
      next_index_ = (index + 1) % entries_.size();
      return entries_[index]->mailbox_.get();
    }
  }

  return nullptr;
}

auto GatewayConnectionPool::NextEventCapable(event::DeliveryMode* mode) -> io::OutboundMailbox* {
  prune();

  for (std::size_t offset = 0; offset < entries_.size(); ++offset) {
    const std::size_t index = (next_index_ + offset) % entries_.size();
    const auto& entry = entries_[index];
    if (entry->live_ && !entry->gateway_id_.empty()) {
      next_index_ = (index + 1) % entries_.size();
      if (mode != nullptr) {
        *mode = entry->delivery_mode_;
      }
      return entry->mailbox_.get();
    }
  }

  return nullptr;
}

void GatewayConnectionPool::Broadcast(std::span<const std::byte> frame,
                                      io::OutboundMailbox* exclude) {
  prune();

  for (const auto& entry : entries_) {
    if (!entry->live_ || entry->mailbox_.get() == exclude) {
      continue;
    }

    entry->mailbox_->Enqueue(std::vector<std::byte>(frame.begin(), frame.end()));
  }
}

void GatewayConnectionPool::SetGatewayId(io::OutboundMailbox* mailbox, std::string gateway_id,
                                         event::DeliveryMode delivery_mode) {
  prune();

  for (const auto& entry : entries_) {
    if (entry->live_ && entry->gateway_id_ == gateway_id && entry->mailbox_.get() != mailbox) {
      entry->gateway_id_.clear();
    }
  }

  for (const auto& entry : entries_) {
    if (entry->live_ && entry->mailbox_.get() == mailbox) {
      entry->gateway_id_ = std::move(gateway_id);
      entry->delivery_mode_ = delivery_mode;
      return;
    }
  }
}

auto GatewayConnectionPool::Find(std::string_view gateway_id) -> io::OutboundMailbox* {
  prune();

  for (const auto& entry : entries_) {
    if (entry->live_ && entry->gateway_id_ == gateway_id) {
      return entry->mailbox_.get();
    }
  }

  return nullptr;
}

} // namespace strij::nodeagent
