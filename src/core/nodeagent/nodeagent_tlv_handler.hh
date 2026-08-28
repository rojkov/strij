#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"
#include "core/node/capabilities.pb.h"
#include "extensions/schedulers/scheduler.hh"

namespace strij::nodeagent {

// Dispatches inbound TLV frames received from a single gateway connection. On
// construction a dispatch table is built from the schedulers' HandledFrameTypes()
// declarations; a frame with no owning scheduler is dropped with a warning. A
// frame routed to a scheduler is that scheduler's to interpret — the handler
// never parses kTaskSubmission itself.
class NodeagentTlvHandler {
public:
  // `schedulers` must outlive this handler; ownership stays with the caller.
  NodeagentTlvHandler(std::span<extensions::Scheduler*> schedulers,
                      std::shared_ptr<const node::NodeCapabilities> capabilities);

  // Serializes and writes the kNodeAdvertisement frame to the connection. Must
  // be the first frame sent on every accepted gateway connection.
  void SendAdvertisement(io::Connection& conn);

  // Routes `frame` to the scheduler that declared its type_id in
  // HandledFrameTypes(). Frames with no owning scheduler are dropped with a
  // warning.
  void HandleFrame(io::TlvFrame frame, io::Connection& conn);

private:
  std::unordered_map<uint8_t, extensions::Scheduler*> dispatcher_table_;
  std::shared_ptr<const node::NodeCapabilities> capabilities_;
};

} // namespace strij::nodeagent