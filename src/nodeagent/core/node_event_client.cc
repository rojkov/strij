#include "nodeagent/core/node_event_client.hh"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/event/cloudevent.pb.h"
#include "common/event/event_plane.pb.h"

namespace strij::nodeagent {

namespace {

class NoopSubscription final : public EventSubscription {
public:
  void Ack(std::string_view /*token*/) override {}
  void Detach() override {}
  void Delete() override {}
};

auto asBytes(std::string_view text) -> std::span<const std::byte> {
  return std::as_bytes(std::span(text.data(), text.size()));
}

} // namespace

class NodeEventClient::Subscription final : public EventSubscription {
public:
  Subscription(NodeEventClient& owner, std::string subscription_id, io::OutboundMailbox* mailbox,
               event::DeliveryMode mode, ReadyCallback on_ready, EventCallback on_event,
               ErrorCallback on_error)
      : owner_{owner}, subscription_id_{std::move(subscription_id)}, mailbox_{mailbox}, mode_{mode},
        on_ready_{std::move(on_ready)}, on_event_{std::move(on_event)},
        on_error_{std::move(on_error)} {}

  ~Subscription() override { remove(true); }

  Subscription(const Subscription&) = delete;
  auto operator=(const Subscription&) -> Subscription& = delete;
  Subscription(Subscription&&) noexcept = delete;
  auto operator=(Subscription&&) noexcept -> Subscription& = delete;

  void Ack(std::string_view token) override {
    if (removed_ || closed_ || mailbox_ == nullptr || mode_ != event::DELIVERY_MODE_AT_LEAST_ONCE) {
      return;
    }

    event::EventAck ack;
    ack.set_subscription_id(subscription_id_);
    ack.set_token(std::string(token));

    std::string serialized;
    if (!ack.SerializeToString(&serialized)) {
      return;
    }

    mailbox_->Enqueue(io::SerializeTlvFrame(io::TlvFrame::kEventAck, asBytes(serialized)));
  }

  void Detach() override { remove(false); }

  void Delete() override { remove(true); }

  void deliver(const event::CloudEvent& event, std::string_view token) {
    if (removed_) {
      return;
    }
    if (on_event_) {
      on_event_(event, token);
    }
  }

  void ready(absl::Status status, event::DeliveryMode mode) {
    if (ready_reported_) {
      return;
    }

    ready_reported_ = true;
    mode_ = mode;
    if (on_ready_) {
      on_ready_(std::move(status), mode);
    }
  }

  // The gateway rejected the subscription request. Remove it locally without
  // sending an unsubscribe (the gateway never registered it).
  void reject(absl::Status status, event::DeliveryMode mode) {
    if (removed_) {
      return;
    }

    removed_ = true;
    owner_.removeSubscription(*this, false);
    ready(std::move(status), mode);
  }

  // The gateway connection closed. Report the failure exactly once: through
  // `on_ready` if the subscription was not ready yet, otherwise through
  // `on_error` so a waiting listen can fail with UNAVAILABLE.
  void close() {
    if (removed_) {
      return;
    }

    closed_ = true;
    removed_ = true;
    owner_.removeSubscription(*this, false);

    absl::Status status = absl::UnavailableError("gateway connection lost");
    if (ready_reported_) {
      if (on_error_) {
        on_error_(std::move(status));
      }
      return;
    }

    ready(std::move(status), mode_);
  }

private:
  void remove(bool notify_gateway) {
    if (removed_) {
      return;
    }

    removed_ = true;
    owner_.removeSubscription(*this, notify_gateway);
  }

  NodeEventClient& owner_;
  std::string subscription_id_;
  io::OutboundMailbox* mailbox_;
  event::DeliveryMode mode_;
  ReadyCallback on_ready_;
  EventCallback on_event_;
  ErrorCallback on_error_;
  std::size_t close_token_{0};
  bool ready_reported_{false};
  bool closed_{false};
  bool removed_{false};

  friend class NodeEventClient;
};

class NodeEventClient::SubscriptionHandle final : public EventSubscription {
public:
  explicit SubscriptionHandle(std::shared_ptr<Subscription> subscription)
      : subscription_{std::move(subscription)} {}
  ~SubscriptionHandle() override { subscription_->Delete(); }

  void Ack(std::string_view token) override { subscription_->Ack(token); }
  void Detach() override { subscription_->Detach(); }
  void Delete() override { subscription_->Delete(); }

private:
  std::shared_ptr<Subscription> subscription_;
};

NodeEventClient::~NodeEventClient() = default;

auto NodeEventClient::Publish(event::CloudEvent event) -> absl::Status {
  io::OutboundMailbox* mailbox = connections_.NextEventCapable();
  if (mailbox == nullptr) {
    if (connections_.LiveCount() == 0) {
      return absl::UnavailableError("no live gateway connection for event publish");
    }
    return absl::UnimplementedError("the gateway has no event plane configured");
  }

  std::string serialized;
  if (!event.SerializeToString(&serialized)) {
    return absl::InternalError("failed to serialize CloudEvent");
  }

  mailbox->Enqueue(io::SerializeTlvFrame(io::TlvFrame::kEventPublish, asBytes(serialized)));
  return absl::OkStatus();
}

auto NodeEventClient::Subscribe(event::EventSubscriptionRequest request, ReadyCallback on_ready,
                                EventCallback on_event, ErrorCallback on_error)
    -> EventSubscriptionPtr {
  if (request.subscription_id().empty()) {
    std::move(on_ready)(absl::InvalidArgumentError("subscription id must not be empty"),
                        event::DELIVERY_MODE_BEST_EFFORT);
    return std::make_unique<NoopSubscription>();
  }

  if (request.delivery_mode() != event::DELIVERY_MODE_AUTO &&
      request.delivery_mode() != event::DELIVERY_MODE_BEST_EFFORT &&
      request.delivery_mode() != event::DELIVERY_MODE_AT_LEAST_ONCE) {
    std::move(on_ready)(absl::InvalidArgumentError("unknown delivery mode"),
                        event::DELIVERY_MODE_BEST_EFFORT);
    return std::make_unique<NoopSubscription>();
  }

  event::DeliveryMode mode = event::DELIVERY_MODE_BEST_EFFORT;
  io::OutboundMailbox* mailbox = connections_.NextEventCapable(&mode);
  if (mailbox == nullptr) {
    absl::Status status =
        connections_.LiveCount() == 0
            ? absl::UnavailableError("no live gateway connection for event subscribe")
            : absl::UnimplementedError("the gateway has no event plane configured");
    std::move(on_ready)(std::move(status), event::DELIVERY_MODE_BEST_EFFORT);
    return std::make_unique<NoopSubscription>();
  }

  if (request.delivery_mode() == event::DELIVERY_MODE_AT_LEAST_ONCE &&
      mode != event::DELIVERY_MODE_AT_LEAST_ONCE) {
    std::move(on_ready)(absl::UnimplementedError("the event plane is best-effort only"), mode);
    return std::make_unique<NoopSubscription>();
  }

  if (subscriptions_.contains(request.subscription_id())) {
    std::move(on_ready)(absl::AlreadyExistsError("subscription id is already active"), mode);
    return std::make_unique<NoopSubscription>();
  }

  const std::string subscription_id = request.subscription_id();
  auto subscription =
      std::make_shared<Subscription>(*this, subscription_id, mailbox, mode, std::move(on_ready),
                                     std::move(on_event), std::move(on_error));
  subscriptions_[subscription_id] = subscription;

  std::string serialized;
  if (!request.SerializeToString(&serialized)) {
    subscriptions_.erase(subscription_id);
    subscription->ready(absl::InternalError("failed to serialize subscription request"), mode);
    return std::make_unique<NoopSubscription>();
  }

  mailbox->Enqueue(io::SerializeTlvFrame(io::TlvFrame::kEventSubscribe, asBytes(serialized)));

  // Register the close hook last. If the connection is already gone it fires
  // immediately and reports the failure through `on_ready`.
  std::weak_ptr<Subscription> weak_subscription = subscription;
  subscription->close_token_ = mailbox->RegisterOnClose([weak_subscription]() {
    if (auto live = weak_subscription.lock()) {
      live->close();
    }
  });

  return std::make_unique<SubscriptionHandle>(std::move(subscription));
}

void NodeEventClient::HandleDelivery(const io::TlvFrame& frame) {
  event::EventDelivery delivery;
  if (!delivery.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                               static_cast<int>(frame.value.size()))) {
    return;
  }

  auto iter = subscriptions_.find(delivery.subscription_id());
  if (iter == subscriptions_.end()) {
    return;
  }

  std::shared_ptr<Subscription> subscription = iter->second;
  subscription->deliver(delivery.event(), delivery.token());
}

void NodeEventClient::HandleSubscribeResult(const io::TlvFrame& frame) {
  event::EventSubscriptionResult result;
  if (!result.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                             static_cast<int>(frame.value.size()))) {
    return;
  }

  auto iter = subscriptions_.find(result.subscription_id());
  if (iter == subscriptions_.end()) {
    return;
  }

  std::shared_ptr<Subscription> subscription = iter->second;
  if (!result.accepted()) {
    subscription->reject(absl::InvalidArgumentError(result.reason()), result.delivery_mode());
    return;
  }

  subscription->ready(absl::OkStatus(), result.delivery_mode());
}

void NodeEventClient::removeSubscription(Subscription& subscription, bool notify_gateway) {
  auto iter = subscriptions_.find(subscription.subscription_id_);
  if (iter != subscriptions_.end() && iter->second.get() == &subscription) {
    subscriptions_.erase(iter);
  }

  if (!subscription.closed_ && subscription.close_token_ != 0 && subscription.mailbox_ != nullptr) {
    subscription.mailbox_->UnregisterOnClose(subscription.close_token_);
  }

  if (!notify_gateway || subscription.closed_ || subscription.mailbox_ == nullptr) {
    return;
  }

  event::EventUnsubscribe unsubscribe;
  unsubscribe.set_subscription_id(subscription.subscription_id_);
  std::string serialized;
  if (unsubscribe.SerializeToString(&serialized)) {
    subscription.mailbox_->Enqueue(
        io::SerializeTlvFrame(io::TlvFrame::kEventUnsubscribe, asBytes(serialized)));
  }
}

} // namespace strij::nodeagent
