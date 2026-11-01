#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "strij/common/pure.hh"

#include "absl/status/status.h"
#include "common/event/cloudevent.pb.h"
#include "common/event/event_plane.pb.h"

namespace strij::nodeagent {

// A live event subscription. Ack is a no-op for a best-effort plane; a durable
// plane uses the token to advance a consumer offset. Detach means the
// connection was lost; Delete means the subscription reached an explicit end.
class EventSubscription {
public:
  EventSubscription() = default;
  virtual ~EventSubscription() = default;

  EventSubscription(const EventSubscription&) = delete;
  auto operator=(const EventSubscription&) -> EventSubscription& = delete;
  EventSubscription(EventSubscription&&) noexcept = delete;
  auto operator=(EventSubscription&&) noexcept -> EventSubscription& = delete;

  virtual void Ack(std::string_view token) PURE;
  virtual void Detach() PURE;
  virtual void Delete() PURE;
};

using EventSubscriptionPtr = std::unique_ptr<EventSubscription>;

// The node-facing event port. A handler publishes CloudEvents and subscribes
// with a coarse pattern. The ready callback reports the guarantee the
// configured event plane actually provides.
class EventBus {
public:
  using ReadyCallback =
      std::move_only_function<void(absl::Status status, event::DeliveryMode actual_mode)>;
  using EventCallback =
      std::move_only_function<void(const event::CloudEvent& event, std::string_view token)>;

  EventBus() = default;
  virtual ~EventBus() = default;

  EventBus(const EventBus&) = delete;
  auto operator=(const EventBus&) -> EventBus& = delete;
  EventBus(EventBus&&) noexcept = delete;
  auto operator=(EventBus&&) noexcept -> EventBus& = delete;

  virtual auto Publish(event::CloudEvent event) -> absl::Status PURE;
  // Returns an immediately usable handle; readiness is reported through
  // `on_ready`. The bus must invoke `on_ready` exactly once.
  virtual auto Subscribe(event::EventSubscriptionRequest request, ReadyCallback on_ready,
                         EventCallback on_event) -> EventSubscriptionPtr PURE;
};

} // namespace strij::nodeagent
