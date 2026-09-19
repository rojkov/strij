#include "nodeagent/core/nodeagent_tlv_handler.hh"

#include <bit>
#include <cstdint>
#include <span>
#include <utility>

#include "absl/status/status.h"
#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/logging/log.hh"
#include "common/node/capabilities.pb.h"
#include "strij/extensions/scheduler.hh"

namespace strij::nodeagent {

NodeagentTlvHandler::NodeagentTlvHandler(
    extensions::Scheduler* router, std::shared_ptr<const node::NodeCapabilities> capabilities)
    : scheduler_{router}, capabilities_{std::move(capabilities)} {}

void NodeagentTlvHandler::SendAdvertisement(io::Connection& conn) {
  std::string serialized;
  capabilities_->SerializeToString(&serialized);
  auto frame =
      io::SerializeTlvFrame(io::TlvFrame::kNodeAdvertisement,
                            std::as_bytes(std::span(serialized.data(), serialized.size())));
  conn.Write(frame);
}

void NodeagentTlvHandler::HandleFrame(io::TlvFrame frame, io::Connection& conn) {
  // Frame-routing seam: a frame type the built-in cases (advertisement, sent
  // outbound only) don't own is the router's to handle — the router reports
  // NotFound when no constituent claims the type. This mirrors the gateway
  // GatewayTlvHandler's default seam (child-outcome frames are claimed by the
  // bundled "default" scheduler through the same route).
  if (scheduler_ == nullptr) {
    LOG_WARNING("No scheduler installed to handle frame type {}",
                static_cast<int>(frame.type_id));
    return;
  }

  const absl::Status status = scheduler_->HandleFrame(frame, conn);
  if (!status.ok()) {
    LOG_WARNING("Scheduler dropped frame type {}: {}", static_cast<int>(frame.type_id),
                status.message());
  }
}

} // namespace strij::nodeagent