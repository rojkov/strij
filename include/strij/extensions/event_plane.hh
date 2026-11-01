#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/event/cloudevent.pb.h"
#include "common/event/event_plane.pb.h"
#include "google/protobuf/message.h"
#include "strij/common/pure.hh"
#include "strij/event/dispatcher.hh"
#include "strij/extensions/factory_context.hh"
#include "strij/extensions/gateway_transport.hh"

namespace strij::extensions {

// The gateway edge's callback for a local subscription.
class EventPlaneHost {
public:
  EventPlaneHost() = default;
  virtual ~EventPlaneHost() = default;

  EventPlaneHost(const EventPlaneHost&) = delete;
  auto operator=(const EventPlaneHost&) -> EventPlaneHost& = delete;
  EventPlaneHost(EventPlaneHost&&) noexcept = delete;
  auto operator=(EventPlaneHost&&) noexcept -> EventPlaneHost& = delete;

  virtual void Deliver(std::string_view subscription_id, const event::CloudEvent& event,
                       std::string_view token) PURE;
};

// A gateway-side distributed event plane. The contract is backend-agnostic:
// offsets and tokens are opaque, and an external-backbone extension or a
// future self-hosted log can both implement it.
class EventPlane : public GatewayTransportReceiver {
public:
  EventPlane() = default;
  ~EventPlane() override = default;

  EventPlane(const EventPlane&) = delete;
  auto operator=(const EventPlane&) -> EventPlane& = delete;
  EventPlane(EventPlane&&) noexcept = delete;
  auto operator=(EventPlane&&) noexcept -> EventPlane& = delete;

  virtual void Publish(event::CloudEvent event) PURE;
  virtual void AddSubscription(std::string subscription_id, event::EventPattern pattern) PURE;
  virtual void AttachSubscription(std::string_view subscription_id) PURE;
  virtual void DetachSubscription(std::string_view subscription_id) PURE;
  virtual void RemoveSubscription(std::string_view subscription_id) PURE;
  virtual void Ack(std::string_view subscription_id, std::string_view token) PURE;

  [[nodiscard]] virtual auto SupportedDeliveryMode() const -> event::DeliveryMode PURE;
  virtual auto ResolveDeliveryMode(event::DeliveryMode requested)
      -> absl::StatusOr<event::DeliveryMode> PURE;
};

using EventPlanePtr = std::unique_ptr<EventPlane>;

struct EventPlaneDeps {
  EventPlaneDeps(event::Dispatcher& dispatcher_ref, std::string gateway_id_value,
                 GatewayTransport& transport_ref, EventPlaneHost& host_ref,
                 event::DispatcherSharedPtr shared_dispatcher_value)
      : dispatcher_{dispatcher_ref}, gateway_id_{std::move(gateway_id_value)},
        transport_{transport_ref}, host_{host_ref},
        shared_dispatcher_{std::move(shared_dispatcher_value)} {}
  ~EventPlaneDeps() = default;

  EventPlaneDeps(const EventPlaneDeps&) = delete;
  auto operator=(const EventPlaneDeps&) -> EventPlaneDeps& = delete;
  EventPlaneDeps(EventPlaneDeps&&) noexcept = delete;
  auto operator=(EventPlaneDeps&&) noexcept -> EventPlaneDeps& = delete;

  event::Dispatcher& dispatcher_;
  std::string gateway_id_;
  GatewayTransport& transport_;
  EventPlaneHost& host_;
  event::DispatcherSharedPtr shared_dispatcher_;
};

// Registered in extensions::Registry<EventPlaneFactory> and selected by
// GatewayConfig.event_plane.
class EventPlaneFactory {
public:
  using MessagePtr = std::unique_ptr<::google::protobuf::Message>;

  EventPlaneFactory() = default;
  virtual ~EventPlaneFactory() = default;

  EventPlaneFactory(const EventPlaneFactory&) = delete;
  auto operator=(const EventPlaneFactory&) -> EventPlaneFactory& = delete;
  EventPlaneFactory(EventPlaneFactory&&) noexcept = delete;
  auto operator=(EventPlaneFactory&&) noexcept -> EventPlaneFactory& = delete;

  [[nodiscard]] virtual auto Name() const -> std::string PURE;
  virtual auto CreateEmptyConfigProto() -> MessagePtr PURE;
  virtual auto Create(const ::google::protobuf::Message& config,
                      GatewayFactoryContext& context, const EventPlaneDeps& deps)
      -> EventPlanePtr PURE;
};

} // namespace strij::extensions
