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
               EventCallback on_event)
      : owner_{owner}, subscription_id_{std::move(subscription_id)}, mailbox_{mailbox},
        on_event_{std::move(on_event)} {}

  ~Subscription() override { remove(true); }

  Subscription(const Subscription&) = delete;
  auto operator=(const Subscription&) -> Subscription& = delete;
  Subscription(Subscription&&) noexcept = delete;
  auto operator=(Subscription&&) noexcept -> Subscription& = delete;

  void Ack(std::string_view /*token*/) override {}

  void Detach() override { remove(false); }

  void Delete() override { remove(true); }

  void deliver(const event::CloudEvent& event, std::string_view token) {
    if (on_event_) {
      on_event_(event, token);
    }
  }

  [[nodiscard]] auto mailbox() const -> io::OutboundMailbox* { return mailbox_; }

private:
  void remove(bool notify_gateway) {
    if (removed_) {
      return;
    }

    removed_ = true;
    owner_.removeSubscription(subscription_id_, notify_gateway);
  }

  NodeEventClient& owner_;
  std::string subscription_id_;
  io::OutboundMailbox* mailbox_;
  EventCallback on_event_;
  bool removed_{false};
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
  io::OutboundMailbox* mailbox = connections_.Next();
  if (mailbox == nullptr) {
    return absl::UnavailableError("no live gateway connection for event publish");
  }

  std::string serialized;
  if (!event.SerializeToString(&serialized)) {
    return absl::InternalError("failed to serialize CloudEvent");
  }

  mailbox->Enqueue(io::SerializeTlvFrame(io::TlvFrame::kEventPublish, asBytes(serialized)));
  return absl::OkStatus();
}

auto NodeEventClient::Subscribe(event::EventSubscriptionRequest request, ReadyCallback on_ready,
                                EventCallback on_event) -> EventSubscriptionPtr {
  if (request.subscription_id().empty()) {
    std::move(on_ready)(absl::InvalidArgumentError("subscription id must not be empty"),
                        event::DELIVERY_MODE_BEST_EFFORT);
    return std::make_unique<NoopSubscription>();
  }

  if (request.delivery_mode() == event::DELIVERY_MODE_AT_LEAST_ONCE) {
    std::move(on_ready)(absl::UnimplementedError("this event plane is best-effort only"),
                        event::DELIVERY_MODE_BEST_EFFORT);
    return std::make_unique<NoopSubscription>();
  }

  if (request.delivery_mode() != event::DELIVERY_MODE_AUTO &&
      request.delivery_mode() != event::DELIVERY_MODE_BEST_EFFORT) {
    std::move(on_ready)(absl::InvalidArgumentError("unknown delivery mode"),
                        event::DELIVERY_MODE_BEST_EFFORT);
    return std::make_unique<NoopSubscription>();
  }

  if (subscriptions_.contains(request.subscription_id())) {
    std::move(on_ready)(absl::AlreadyExistsError("subscription id is already active"),
                        event::DELIVERY_MODE_BEST_EFFORT);
    return std::make_unique<NoopSubscription>();
  }

  io::OutboundMailbox* mailbox = connections_.Next();
  if (mailbox == nullptr) {
    std::move(on_ready)(absl::UnavailableError("no live gateway connection for event subscribe"),
                        event::DELIVERY_MODE_BEST_EFFORT);
    return std::make_unique<NoopSubscription>();
  }

  auto subscription = std::make_shared<Subscription>(*this, request.subscription_id(), mailbox,
                                                     std::move(on_event));
  subscriptions_[request.subscription_id()] = subscription;

  std::string serialized;
  if (!request.SerializeToString(&serialized)) {
    subscriptions_.erase(request.subscription_id());
    std::move(on_ready)(absl::InternalError("failed to serialize subscription request"),
                        event::DELIVERY_MODE_BEST_EFFORT);
    return std::make_unique<NoopSubscription>();
  }

  mailbox->Enqueue(io::SerializeTlvFrame(io::TlvFrame::kEventSubscribe, asBytes(serialized)));
  std::move(on_ready)(absl::OkStatus(), event::DELIVERY_MODE_BEST_EFFORT);
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

void NodeEventClient::removeSubscription(std::string_view subscription_id, bool notify_gateway) {
  auto iter = subscriptions_.find(std::string(subscription_id));
  if (iter == subscriptions_.end()) {
    return;
  }

  Subscription* subscription = iter->second.get();
  subscriptions_.erase(iter);

  if (!notify_gateway || subscription->mailbox() == nullptr) {
    return;
  }

  event::EventUnsubscribe unsubscribe;
  unsubscribe.set_subscription_id(std::string(subscription_id));
  std::string serialized;
  if (unsubscribe.SerializeToString(&serialized)) {
    subscription->mailbox()->Enqueue(
        io::SerializeTlvFrame(io::TlvFrame::kEventUnsubscribe, asBytes(serialized)));
  }
}

} // namespace strij::nodeagent
