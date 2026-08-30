#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "common/task/task.pb.h"
#include "google/protobuf/message.h"
#include "strij/common/pure.hh"
#include "strij/extensions/factory_context.hh"
#include "strij/gateway/result_receiver_storage.hh"

namespace strij {

// Framework io types are forward-declared here (the accepted Connection
// exclusion): HandleFrame names them in its signature, but the definitions live
// in src/ (see TODO on narrowing the seam).
namespace io {
class Connection;
struct TlvFrame;
} // namespace io

namespace extensions {

// A scheduling protocol extension, shared by the gateway and nodeagent halves
// of the same wire protocol ("push" in v1). The gateway side submits tasks via
// Schedule(); the optional TLV-frame facet (HandleFrame + HandledFrameTypes)
// lets a scheduler own its side of a bidirectional protocol and makes the
// nodeagent's inbound frame dispatcher protocol-agnostic.
//
// Contract: every task handed to Schedule() MUST eventually resolve through
// `receiver` — either a result (Deliver with is_final=true) or an error
// (DeliverError) — so the receiver never hangs. A gateway-side scheduler routes
// to nodes whose advertisement lists RequiredProtocol() in
// scheduling_protocols.
class Scheduler {
public:
  Scheduler() = default;
  virtual ~Scheduler() = default;

  Scheduler(const Scheduler&) = delete;
  auto operator=(const Scheduler&) -> Scheduler& = delete;
  Scheduler(Scheduler&&) noexcept = delete;
  auto operator=(Scheduler&&) noexcept -> Scheduler& = delete;

  // Fire-and-forget: submits `task` to the scheduling fabric, taking ownership
  // of `receiver`. Must not block on a node round-trip.
  virtual void Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) PURE;
  // The scheduling_protocol a candidate node must advertise (v1: "push").
  [[nodiscard]] virtual auto RequiredProtocol() const -> std::string_view PURE;
  // Routes an inbound wire frame of one of the types returned by
  // HandledFrameTypes(). Returns OkStatus when the frame was handled (or was a
  // legitimate no-op); a non-ok Status (e.g. NotFound when the frame type is
  // unclaimed) signals the frame was dropped. Defaults to a no-op for schedulers
  // with no inbound frames (pure gateway-side policies).
  //
  // TODO(seam): narrow the `io::Connection&` dependency off the extension
  // surface. Extension authors can't depend on the framework's concrete
  // io::Connection; replace it with a small abstract connection/seam type so
  // nodeagent schedulers never leak the framework type.
  virtual auto HandleFrame(const io::TlvFrame& /*frame*/, io::Connection& /*conn*/) -> absl::Status {
    return absl::OkStatus();
  }
  // The TLV frame type_ids this scheduler owns (e.g. {kTaskSubmission} for
  // "push"). Empty = the scheduler never receives frames. Frame-type ownership
  // is disjoint across scheduling protocols, so the nodeagent dispatcher can
  // route by type_id alone.
  [[nodiscard]] virtual auto HandledFrameTypes() const -> std::span<const uint8_t> { return {}; }
};

using SchedulerPtr = std::unique_ptr<Scheduler>;

} // namespace strij::extensions

} // namespace strij

namespace strij::gateway {

// Gateway-side scheduler factory, registered in
// extensions::Registry<gateway::GatewaySchedulerFactory>.
class GatewaySchedulerFactory {
public:
  using MessagePtr = std::unique_ptr<::google::protobuf::Message>;

  GatewaySchedulerFactory() = default;
  virtual ~GatewaySchedulerFactory() = default;

  GatewaySchedulerFactory(const GatewaySchedulerFactory&) = delete;
  auto operator=(const GatewaySchedulerFactory&) -> GatewaySchedulerFactory& = delete;
  GatewaySchedulerFactory(GatewaySchedulerFactory&&) noexcept = delete;
  auto operator=(GatewaySchedulerFactory&&) noexcept -> GatewaySchedulerFactory& = delete;

  [[nodiscard]] virtual auto Name() const -> std::string PURE;
  virtual auto CreateEmptyConfigProto() -> MessagePtr PURE;
  virtual auto Create(const ::google::protobuf::Message& config,
                      extensions::GatewayFactoryContext& context) -> extensions::SchedulerPtr PURE;
};

} // namespace strij::gateway

namespace strij::nodeagent {

// Nodeagent-side scheduler factory, registered in
// extensions::Registry<nodeagent::NodeSchedulerFactory>.
class NodeSchedulerFactory {
public:
  using MessagePtr = std::unique_ptr<::google::protobuf::Message>;

  NodeSchedulerFactory() = default;
  virtual ~NodeSchedulerFactory() = default;

  NodeSchedulerFactory(const NodeSchedulerFactory&) = delete;
  auto operator=(const NodeSchedulerFactory&) -> NodeSchedulerFactory& = delete;
  NodeSchedulerFactory(NodeSchedulerFactory&&) noexcept = delete;
  auto operator=(NodeSchedulerFactory&&) noexcept -> NodeSchedulerFactory& = delete;

  [[nodiscard]] virtual auto Name() const -> std::string PURE;
  // The scheduling_protocol this scheduler implements; appended verbatim to the
  // node's NodeCapabilities.scheduling_protocols (see BuildNodeCapabilities).
  [[nodiscard]] virtual auto RequiredProtocol() const -> std::string_view PURE;
  virtual auto CreateEmptyConfigProto() -> MessagePtr PURE;
  virtual auto Create(const ::google::protobuf::Message& config,
                      extensions::NodeagentFactoryContext& context)
      -> extensions::SchedulerPtr PURE;
};

} // namespace strij::nodeagent