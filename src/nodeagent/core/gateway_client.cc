#include "nodeagent/core/gateway_client.hh"

#include <algorithm>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/task/task.pb.h"

namespace strij::nodeagent {

auto GatewayClient::Forward(const task::Task& task) -> absl::Status {
  // Remove dead mailboxes (those whose connections have closed).
  live_mailboxes_.erase(std::remove_if(live_mailboxes_.begin(), live_mailboxes_.end(),
                                       [](io::OutboundMailbox* mbox) { return mbox == nullptr; }),
                        live_mailboxes_.end());

  if (live_mailboxes_.empty()) {
    return absl::UnavailableError("no live gateway connection to forward child task '" + task.id() +
                                  "'");
  }

  io::OutboundMailbox* mailbox = live_mailboxes_[next_index_ % live_mailboxes_.size()];
  next_index_ = (next_index_ + 1) % live_mailboxes_.size();

  std::string serialized;
  if (!task.SerializeToString(&serialized)) {
    return absl::InternalError("failed to serialize child task '" + task.id() + "' for forwarding");
  }

  auto frame =
      io::SerializeTlvFrame(io::TlvFrame::kTaskSubmission,
                            std::as_bytes(std::span(serialized.data(), serialized.size())));
  mailbox->Enqueue(std::move(frame));
  return absl::OkStatus();
}

void GatewayClient::RegisterConnection(std::shared_ptr<io::OutboundMailbox> mailbox) {
  live_mailboxes_.push_back(mailbox.get());
  // Register a close callback to null out the pointer when the connection tears down.
  mailbox->RegisterOnClose([this, mbox = std::move(mailbox)]() {
    auto iter = std::find(live_mailboxes_.begin(), live_mailboxes_.end(), mbox.get());
    if (iter != live_mailboxes_.end()) {
      *iter = nullptr;
    }
  });
}

auto GatewayClient::LiveCount() const -> size_t {
  return std::count_if(live_mailboxes_.begin(), live_mailboxes_.end(),
                       [](io::OutboundMailbox* m) { return m != nullptr; });
}

} // namespace strij::nodeagent