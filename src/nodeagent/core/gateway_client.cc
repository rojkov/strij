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

GatewayClient::~GatewayClient() {
  for (auto& registration : live_mailboxes_) {
    if (registration.mailbox_ != nullptr) {
      registration.mailbox_->UnregisterOnClose(registration.close_token_);
    }
  }
}

auto GatewayClient::Forward(const task::Task& task) -> absl::Status {
  // Remove dead mailboxes (those whose connections have closed).
  live_mailboxes_.erase(std::remove_if(live_mailboxes_.begin(), live_mailboxes_.end(),
                                       [](const MailboxRegistration& registration) {
                                         return registration.mailbox_ == nullptr;
                                       }),
                        live_mailboxes_.end());

  if (live_mailboxes_.empty()) {
    return absl::UnavailableError("no live gateway connection to forward child task '" + task.id() +
                                  "'");
  }

  io::OutboundMailbox* mailbox =
      live_mailboxes_[next_index_ % live_mailboxes_.size()].mailbox_.get();
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
  live_mailboxes_.push_back({.mailbox_ = std::move(mailbox), .close_token_ = 0});
  auto& registration = live_mailboxes_.back();
  io::OutboundMailbox* registered_mailbox = registration.mailbox_.get();
  registration.close_token_ = registration.mailbox_->RegisterOnClose([this, registered_mailbox]() {
    auto iter = std::find_if(live_mailboxes_.begin(), live_mailboxes_.end(),
                             [registered_mailbox](const MailboxRegistration& entry) {
                               return entry.mailbox_.get() == registered_mailbox;
                             });
    if (iter != live_mailboxes_.end()) {
      iter->mailbox_.reset();
    }
  });
}

auto GatewayClient::LiveCount() const -> size_t {
  return std::count_if(
      live_mailboxes_.begin(), live_mailboxes_.end(),
      [](const MailboxRegistration& registration) { return registration.mailbox_ != nullptr; });
}

} // namespace strij::nodeagent