#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "common/event/cloudevent.pb.h"
#include "common/event/event_plane.pb.h"
#include "strij/gateway/event_plane.hh"
#include "strij/gateway/gateway_transport.hh"

namespace strij::event {
class Dispatcher;
}
namespace strij::io {
class PeriodicTimer;
}

namespace strij::gateway::event_planes {

// The bundled best-effort event plane. Each gateway advertises a compact
// summary of the event types and sources its local listeners can consume; a
// publisher forwards an event only to peers whose summary matches.
class SummaryMeshEventPlane final : public EventPlane {
public:
  SummaryMeshEventPlane(const EventPlaneDeps& deps, std::uint64_t refresh_interval_ms,
                        std::uint64_t peer_ttl_ms);
  ~SummaryMeshEventPlane() override;

  SummaryMeshEventPlane(const SummaryMeshEventPlane&) = delete;
  auto operator=(const SummaryMeshEventPlane&) -> SummaryMeshEventPlane& = delete;
  SummaryMeshEventPlane(SummaryMeshEventPlane&&) noexcept = delete;
  auto operator=(SummaryMeshEventPlane&&) noexcept -> SummaryMeshEventPlane& = delete;

  void Publish(event::CloudEvent event) override;
  void AddSubscription(std::string subscription_id, event::EventPattern pattern) override;
  void AttachSubscription(std::string_view subscription_id) override;
  void DetachSubscription(std::string_view subscription_id) override;
  void RemoveSubscription(std::string_view subscription_id) override;
  void Ack(std::string_view subscription_id, std::string_view token) override;

  [[nodiscard]] auto SupportedDeliveryMode() const -> event::DeliveryMode override;
  auto ResolveDeliveryMode(event::DeliveryMode requested)
      -> absl::StatusOr<event::DeliveryMode> override;

  void OnMessage(std::span<const std::byte> payload) override;

  // Test hook: expire peer summaries as of `now`.
  void ExpirePeers(absl::Time now);

private:
  struct PeerSummary {
    std::set<std::string> types;
    std::set<std::string> sources;
    bool match_all{false};
    std::uint64_t epoch{0};
    absl::Time last_seen;
  };

  void broadcastSummary();
  void tick();
  void deliverLocal(const event::CloudEvent& event);
  [[nodiscard]] auto localSubscriptionsFor(const event::CloudEvent& event) const
      -> std::vector<std::string>;
  [[nodiscard]] auto buildSummary() const -> event::RouteSummary;
  [[nodiscard]] static auto patternMatches(const event::EventPattern& pattern,
                                           const event::CloudEvent& event) -> bool;
  [[nodiscard]] static auto peerMatches(const PeerSummary& peer, const event::CloudEvent& event)
      -> bool;

  std::string gateway_id_;
  GatewayTransport& transport_;
  EventPlaneHost& host_;
  std::unique_ptr<io::PeriodicTimer> timer_;
  std::uint64_t refresh_interval_ms_;
  std::uint64_t peer_ttl_ms_;
  std::uint64_t epoch_{0};
  std::map<std::string, event::EventPattern> subscriptions_;
  std::map<std::string, PeerSummary> peers_;
};

class SummaryMeshEventPlaneFactory final : public EventPlaneFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, extensions::GatewayFactoryContext& context,
              const EventPlaneDeps& deps) -> EventPlanePtr override;
};

} // namespace strij::gateway::event_planes
