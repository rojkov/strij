#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "absl/status/status.h"
#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/event/cloudevent.pb.h"
#include "common/event/event_plane.pb.h"
#include "nodeagent/core/event_delivery_sink.hh"
#include "nodeagent/core/gateway_connection_pool.hh"
#include "strij/nodeagent/event_bus.hh"

namespace strij::nodeagent {

// The v1 node event bus. It publishes over the shared gateway pool and keeps
// local subscriptions keyed by subscription id.
class NodeEventClient final : public EventBus, public EventDeliverySink {
public:
  explicit NodeEventClient(GatewayConnectionPool& connections) : connections_{connections} {}
  ~NodeEventClient() override;

  NodeEventClient(const NodeEventClient&) = delete;
  auto operator=(const NodeEventClient&) -> NodeEventClient& = delete;
  NodeEventClient(NodeEventClient&&) noexcept = delete;
  auto operator=(NodeEventClient&&) noexcept -> NodeEventClient& = delete;

  auto Publish(event::CloudEvent event) -> absl::Status override;
  auto Subscribe(event::EventSubscriptionRequest request, ReadyCallback on_ready,
                 EventCallback on_event) -> EventSubscriptionPtr override;

  // Dispatches an inbound kEventDelivery frame.
  void HandleDelivery(const io::TlvFrame& frame) override;

private:
  class Subscription;

  void removeSubscription(std::string_view subscription_id, bool notify_gateway);

  GatewayConnectionPool& connections_;
  std::unordered_map<std::string, Subscription*> subscriptions_;
};

} // namespace strij::nodeagent
