#include "core/nodeagent/nodeagent_tlv_handler.hh"

#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/logging/log.hh"
#include "core/node/capabilities.pb.h"
#include "extensions/schedulers/scheduler.hh"

namespace strij::nodeagent {

NodeagentTlvHandler::NodeagentTlvHandler(std::span<extensions::Scheduler*> schedulers,
                                         std::shared_ptr<const node::NodeCapabilities> capabilities)
    : capabilities_{std::move(capabilities)} {
  for (extensions::Scheduler* scheduler : schedulers) {
    for (const uint8_t type_id : scheduler->HandledFrameTypes()) {
      if (dispatcher_table_.contains(type_id)) {
        // Frame-type ownership is exclusive per scheduling protocol; the first
        // scheduler in config order wins, later claims are logged and skipped.
        LOG_WARNING("Duplicate frame type {} claim ignored", static_cast<int>(type_id));
        continue;
      }

      dispatcher_table_.emplace(type_id, scheduler);
    }
  }
}

void NodeagentTlvHandler::SendAdvertisement(io::Connection& conn) {
  std::string serialized;
  capabilities_->SerializeToString(&serialized);
  auto frame =
      io::SerializeTlvFrame(io::TlvFrame::kNodeAdvertisement,
                            std::as_bytes(std::span(serialized.data(), serialized.size())));
  conn.Write(frame);
}

void NodeagentTlvHandler::HandleFrame(io::TlvFrame frame, io::Connection& conn) {
  auto iter = dispatcher_table_.find(frame.type_id);
  if (iter == dispatcher_table_.end()) {
    LOG_WARNING("No scheduler owns frame type {}", static_cast<int>(frame.type_id));
    return;
  }

  iter->second->HandleFrame(std::move(frame), conn);
}

} // namespace strij::nodeagent