#include "gateway/core/gateway_event_edge.hh"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/logging/log.hh"
#include "common/event/cloudevent.pb.h"
#include "common/event/event_plane.pb.h"
#include "strij/gateway/event_plane.hh"

namespace strij::gateway {

GatewayEventEdge::~GatewayEventEdge() {
  for (const auto& [mailbox, watch] : close_watches_) {
    (void)mailbox;
    watch.mailbox_->UnregisterOnClose(watch.token_);
  }
}

namespace {

auto asBytes(std::string_view text) -> std::span<const std::byte> {
  return std::as_bytes(std::span(text.data(), text.size()));
}

} // namespace

auto GatewayEventEdge::HandlePublishFrame(const io::TlvFrame& frame) -> absl::Status {
  if (plane_ == nullptr) {
    return absl::UnimplementedError("event plane is not configured");
  }

  event::CloudEvent event;
  if (!event.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                            static_cast<int>(frame.value.size()))) {
    return absl::InvalidArgumentError("malformed CloudEvent");
  }

  plane_->Publish(std::move(event));
  return absl::OkStatus();
}

auto GatewayEventEdge::HandleSubscribeFrame(const io::TlvFrame& frame,
                                            const std::shared_ptr<io::OutboundMailbox>& mailbox)
    -> absl::Status {
  if (plane_ == nullptr) {
    return absl::UnimplementedError("event plane is not configured");
  }

  event::EventSubscriptionRequest request;
  if (!request.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                              static_cast<int>(frame.value.size()))) {
    return absl::InvalidArgumentError("malformed EventSubscriptionRequest");
  }

  if (request.subscription_id().empty()) {
    return absl::InvalidArgumentError("subscription id must not be empty");
  }

  const std::string subscription_id = request.subscription_id();
  auto resolved = plane_->ResolveDeliveryMode(request.delivery_mode());
  if (!resolved.ok()) {
    sendSubscriptionResult(mailbox, subscription_id, false, resolved.status().message(),
                           event::DELIVERY_MODE_BEST_EFFORT);
    return resolved.status();
  }

  if (subscriptions_.contains(subscription_id)) {
    sendSubscriptionResult(mailbox, subscription_id, false, "subscription id is already active",
                           *resolved);
    return absl::AlreadyExistsError("subscription id is already active");
  }

  subscriptions_.emplace(subscription_id,
                         Entry{.subscription_id_ = subscription_id, .mailbox_ = mailbox});
  ensureCloseWatched(mailbox);

  plane_->AddSubscription(subscription_id, request.pattern());
  plane_->AttachSubscription(subscription_id);
  sendSubscriptionResult(mailbox, subscription_id, true, {}, *resolved);
  LOG_INFO("Event subscription {} registered (mode={})", subscription_id,
           static_cast<int>(*resolved));
  return absl::OkStatus();
}

void GatewayEventEdge::sendSubscriptionResult(const std::shared_ptr<io::OutboundMailbox>& mailbox,
                                              std::string_view subscription_id, bool accepted,
                                              std::string_view reason,
                                              event::DeliveryMode delivery_mode) {
  event::EventSubscriptionResult result;
  result.set_subscription_id(std::string(subscription_id));
  result.set_accepted(accepted);
  result.set_reason(std::string(reason));
  result.set_delivery_mode(delivery_mode);

  std::string serialized;
  if (!result.SerializeToString(&serialized)) {
    return;
  }

  mailbox->Enqueue(io::SerializeTlvFrame(io::TlvFrame::kEventSubscribeResult, asBytes(serialized)));
}

auto GatewayEventEdge::HandleAckFrame(const io::TlvFrame& frame) -> absl::Status {
  if (plane_ == nullptr) {
    return absl::UnimplementedError("event plane is not configured");
  }

  event::EventAck ack;
  if (!ack.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                          static_cast<int>(frame.value.size()))) {
    return absl::InvalidArgumentError("malformed EventAck");
  }

  plane_->Ack(ack.subscription_id(), ack.token());

  return absl::OkStatus();
}

auto GatewayEventEdge::HandleUnsubscribeFrame(const io::TlvFrame& frame) -> absl::Status {
  if (plane_ == nullptr) {
    return absl::UnimplementedError("event plane is not configured");
  }

  event::EventUnsubscribe unsubscribe;
  if (!unsubscribe.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                                  static_cast<int>(frame.value.size()))) {
    return absl::InvalidArgumentError("malformed EventUnsubscribe");
  }

  auto iter = subscriptions_.find(unsubscribe.subscription_id());
  if (iter == subscriptions_.end()) {
    return absl::NotFoundError("unknown subscription");
  }

  plane_->RemoveSubscription(unsubscribe.subscription_id());
  subscriptions_.erase(iter);

  return absl::OkStatus();
}

void GatewayEventEdge::Deliver(std::string_view subscription_id, const event::CloudEvent& event,
                               std::string_view token) {
  auto iter = subscriptions_.find(std::string(subscription_id));
  if (iter == subscriptions_.end() || iter->second.mailbox_ == nullptr) {
    return;
  }

  event::EventDelivery delivery;
  delivery.set_subscription_id(std::string(subscription_id));
  delivery.set_token(std::string(token));
  *delivery.mutable_event() = event;

  std::string serialized;
  if (!delivery.SerializeToString(&serialized)) {
    return;
  }

  iter->second.mailbox_->Enqueue(
      io::SerializeTlvFrame(io::TlvFrame::kEventDelivery, asBytes(serialized)));
}

void GatewayEventEdge::ensureCloseWatched(const std::shared_ptr<io::OutboundMailbox>& mailbox) {
  if (close_watches_.contains(mailbox.get())) {
    return;
  }

  const std::size_t token =
      mailbox->RegisterOnClose([this, raw = mailbox.get()]() { onConnectionClosed(raw); });
  close_watches_.emplace(mailbox.get(), CloseWatch{.mailbox_ = mailbox, .token_ = token});
}

void GatewayEventEdge::onConnectionClosed(io::OutboundMailbox* mailbox) {
  close_watches_.erase(mailbox);

  for (auto iter = subscriptions_.begin(); iter != subscriptions_.end();) {
    if (iter->second.mailbox_.get() == mailbox) {
      if (plane_ != nullptr) {
        plane_->DetachSubscription(iter->first);
      }
      iter = subscriptions_.erase(iter);
    } else {
      ++iter;
    }
  }
}

} // namespace strij::gateway
