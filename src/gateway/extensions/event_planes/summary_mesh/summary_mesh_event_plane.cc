#include "gateway/extensions/event_planes/summary_mesh/summary_mesh_event_plane.hh"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "common/core/io/periodic_timer.hh"
#include "common/event/cloudevent.pb.h"
#include "common/event/event_plane.pb.h"
#include "gateway/extensions/event_planes/summary_mesh/summary_mesh.pb.h"
#include "strij/extensions/extension_registry.hh"

namespace strij::gateway::event_planes {

namespace {

constexpr std::uint64_t kDefaultRefreshMs = 1000;
constexpr std::uint64_t kDefaultTtlMs = 5000;

auto asBytes(std::string_view text) -> std::span<const std::byte> {
  return std::as_bytes(std::span(text.data(), text.size()));
}

} // namespace

SummaryMeshEventPlane::SummaryMeshEventPlane(const extensions::EventPlaneDeps& deps,
                                             std::uint64_t refresh_interval_ms,
                                             std::uint64_t peer_ttl_ms)
    : gateway_id_{deps.gateway_id_}, transport_{deps.transport_}, host_{deps.host_},
      refresh_interval_ms_{refresh_interval_ms == 0 ? kDefaultRefreshMs : refresh_interval_ms},
      peer_ttl_ms_{peer_ttl_ms == 0 ? kDefaultTtlMs : peer_ttl_ms} {
  transport_.SetReceiver(this);

  if (deps.shared_dispatcher_ != nullptr) {
    timer_ = std::make_unique<io::PeriodicTimer>(deps.shared_dispatcher_, [this]() { tick(); });
    timer_->Start(absl::Milliseconds(refresh_interval_ms_));
  }
}

SummaryMeshEventPlane::~SummaryMeshEventPlane() = default;

auto SummaryMeshEventPlane::patternMatches(const event::EventPattern& pattern,
                                           const event::CloudEvent& event) -> bool {
  if (!pattern.type().empty() && pattern.type() != event.type()) {
    return false;
  }
  if (!pattern.source().empty() && pattern.source() != event.source()) {
    return false;
  }
  if (!pattern.subject().empty() && pattern.subject() != event.subject()) {
    return false;
  }
  return true;
}

auto SummaryMeshEventPlane::peerMatches(const PeerSummary& peer, const event::CloudEvent& event)
    -> bool {
  if (peer.match_all) {
    return true;
  }
  if (peer.types.contains(event.type())) {
    return true;
  }
  return peer.sources.contains(event.source());
}

auto SummaryMeshEventPlane::localSubscriptionsFor(const event::CloudEvent& event) const
    -> std::vector<std::string> {
  std::vector<std::string> matches;
  for (const auto& [subscription_id, pattern] : subscriptions_) {
    if (patternMatches(pattern, event)) {
      matches.push_back(subscription_id);
    }
  }
  return matches;
}

void SummaryMeshEventPlane::deliverLocal(const event::CloudEvent& event) {
  const std::string token = event.source() + ":" + event.id();
  for (const std::string& subscription_id : localSubscriptionsFor(event)) {
    host_.Deliver(subscription_id, event, token);
  }
}

void SummaryMeshEventPlane::Publish(event::CloudEvent event) {
  deliverLocal(event);

  std::vector<std::string_view> targets;
  for (const auto& [peer_id, peer] : peers_) {
    if (peerMatches(peer, event)) {
      targets.push_back(peer_id);
    }
  }

  if (targets.empty()) {
    return;
  }

  event::EventPlaneMessage message;
  message.set_sender_id(gateway_id_);
  message.set_epoch(epoch_);
  *message.mutable_event() = std::move(event);

  std::string serialized;
  if (!message.SerializeToString(&serialized)) {
    return;
  }

  (void)transport_.Send(targets, asBytes(serialized));
}

void SummaryMeshEventPlane::AddSubscription(std::string subscription_id,
                                            event::EventPattern pattern) {
  subscriptions_.insert_or_assign(std::move(subscription_id), std::move(pattern));
  broadcastSummary();
}

void SummaryMeshEventPlane::AttachSubscription(std::string_view /*subscription_id*/) {}

void SummaryMeshEventPlane::DetachSubscription(std::string_view subscription_id) {
  RemoveSubscription(subscription_id);
}

void SummaryMeshEventPlane::RemoveSubscription(std::string_view subscription_id) {
  if (subscriptions_.erase(std::string(subscription_id)) > 0) {
    broadcastSummary();
  }
}

void SummaryMeshEventPlane::Ack(std::string_view /*subscription_id*/,
                                std::string_view /*token*/) {}

auto SummaryMeshEventPlane::SupportedDeliveryMode() const -> event::DeliveryMode {
  return event::DELIVERY_MODE_BEST_EFFORT;
}

auto SummaryMeshEventPlane::ResolveDeliveryMode(event::DeliveryMode requested)
    -> absl::StatusOr<event::DeliveryMode> {
  switch (requested) {
  case event::DELIVERY_MODE_AUTO:
  case event::DELIVERY_MODE_BEST_EFFORT:
    return event::DELIVERY_MODE_BEST_EFFORT;
  case event::DELIVERY_MODE_AT_LEAST_ONCE:
    return absl::UnimplementedError("the summary-mesh event plane is best-effort only");
  default:
    return absl::InvalidArgumentError("unknown delivery mode");
  }
}

auto SummaryMeshEventPlane::buildSummary() const -> event::RouteSummary {
  event::RouteSummary summary;
  for (const auto& [subscription_id, pattern] : subscriptions_) {
    (void)subscription_id;
    if (pattern.type().empty() && pattern.source().empty()) {
      summary.set_match_all(true);
      continue;
    }
    if (!pattern.type().empty()) {
      summary.add_types(pattern.type());
    }
    if (!pattern.source().empty()) {
      summary.add_sources(pattern.source());
    }
  }
  return summary;
}

void SummaryMeshEventPlane::broadcastSummary() {
  event::EventPlaneMessage message;
  message.set_sender_id(gateway_id_);
  message.set_epoch(++epoch_);
  *message.mutable_summary() = buildSummary();

  std::string serialized;
  if (!message.SerializeToString(&serialized)) {
    return;
  }

  (void)transport_.Broadcast(asBytes(serialized));
}

void SummaryMeshEventPlane::ExpirePeers(absl::Time now) {
  const absl::Duration ttl = absl::Milliseconds(peer_ttl_ms_);
  for (auto iter = peers_.begin(); iter != peers_.end();) {
    if (now - iter->second.last_seen > ttl) {
      iter = peers_.erase(iter);
    } else {
      ++iter;
    }
  }
}

void SummaryMeshEventPlane::tick() {
  broadcastSummary();
  ExpirePeers(absl::Now());
}

void SummaryMeshEventPlane::OnMessage(std::span<const std::byte> payload) {
  event::EventPlaneMessage message;
  if (!message.ParseFromArray(std::bit_cast<const char*>(payload.data()),
                              static_cast<int>(payload.size()))) {
    return;
  }

  if (message.sender_id().empty() || message.sender_id() == gateway_id_) {
    return;
  }

  if (message.has_summary()) {
    auto iter = peers_.find(message.sender_id());
    if (iter != peers_.end() && message.epoch() < iter->second.epoch) {
      return;
    }

    PeerSummary peer;
    peer.match_all = message.summary().match_all();
    for (const std::string& type : message.summary().types()) {
      peer.types.insert(type);
    }
    for (const std::string& source : message.summary().sources()) {
      peer.sources.insert(source);
    }
    peer.epoch = message.epoch();
    peer.last_seen = absl::Now();
    peers_.insert_or_assign(message.sender_id(), std::move(peer));
    return;
  }

  if (message.has_event()) {
    deliverLocal(message.event());
  }
}

auto SummaryMeshEventPlaneFactory::Name() const -> std::string { return "summary_mesh"; }

auto SummaryMeshEventPlaneFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<extensions::event_planes::summary_mesh::SummaryMeshConfig>();
}

auto SummaryMeshEventPlaneFactory::Create(const ::google::protobuf::Message& config,
                                          extensions::GatewayFactoryContext& /*context*/,
                                          const extensions::EventPlaneDeps& deps)
    -> extensions::EventPlanePtr {
  std::uint64_t refresh_ms = kDefaultRefreshMs;
  std::uint64_t ttl_ms = kDefaultTtlMs;
  if (const auto* mesh_config = dynamic_cast<
          const extensions::event_planes::summary_mesh::SummaryMeshConfig*>(&config);
      mesh_config != nullptr) {
    refresh_ms = mesh_config->refresh_interval_ms();
    ttl_ms = mesh_config->peer_ttl_ms();
  }

  return std::make_unique<SummaryMeshEventPlane>(deps, refresh_ms, ttl_ms);
}

} // namespace strij::gateway::event_planes

REGISTER_FACTORY_FULLY_QUALIFIED(
    strij::gateway::event_planes::SummaryMeshEventPlaneFactory,
    strij::extensions::EventPlaneFactory, summary_mesh_event_plane_registrar)
