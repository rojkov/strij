#pragma once

#include <memory>

#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/node/capabilities.pb.h"
#include "strij/extensions/scheduler.hh"

namespace strij::nodeagent {

// Dispatches inbound TLV frames received from a single gateway connection.
// This is the node side's frame-routing seam (the mirror of the gateway
// GatewayTlvHandler): every frame is handed to the composite
// NodeagentSchedulerRouter, which routes it to the constituent that declared the
// type in its HandledFrameTypes(). A frame with no owning scheduler surfaces
// as NotFound from the router and is dropped with a warning. The handler never
// parses a frame itself — child-outcome frames (kResult / kTaskRejected) are
// claimed by the bundled "default" scheduler, not by core.
class NodeagentTlvHandler {
public:
  // `router` (the NodeagentSchedulerRouter) must outlive this handler; ownership
  // stays with the caller. A null router (setups without schedulers) turns
  // every frame into an error-drop.
  NodeagentTlvHandler(extensions::Scheduler* router,
                      std::shared_ptr<const node::NodeCapabilities> capabilities);

  // Serializes and writes the kNodeAdvertisement frame to the connection. Must
  // be the first frame sent on every accepted gateway connection.
  void SendAdvertisement(io::Connection& conn);

  // Routes `frame` to the composite router (the scheduler seam). Frames with
  // no owning scheduler are dropped with a warning.
  void HandleFrame(io::TlvFrame frame, io::Connection& conn);

private:
  extensions::Scheduler* scheduler_;
  std::shared_ptr<const node::NodeCapabilities> capabilities_;
};

} // namespace strij::nodeagent