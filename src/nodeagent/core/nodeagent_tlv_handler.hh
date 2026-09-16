#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/node/capabilities.pb.h"
#include "nodeagent/core/local_receiver_registry.hh"
#include "strij/extensions/scheduler.hh"

namespace strij::nodeagent {

// Dispatches inbound TLV frames received from a single gateway connection. On
// construction a dispatch table is built from the schedulers' HandledFrameTypes()
// declarations; a frame with no owning scheduler is dropped with a warning. A
// frame routed to a scheduler is that scheduler's to interpret — the handler
// never parses kTaskSubmission itself.
//
// Two frame types are core-owned (like kNodeAdvertisement), not scheduler-owned:
// kResult and kTaskRejected are child outcomes routed to the node-global
// LocalReceiverRegistry. They are handled before the dispatcher-table lookup and
// never appear in any scheduler's HandledFrameTypes().
class NodeagentTlvHandler {
public:
  // `schedulers` must outlive this handler; ownership stays with the caller.
  // `registry` (optional) receives child-outcome frames; null registry drops
  // them with a warning (kept for existing tests/setups without local children).
  NodeagentTlvHandler(std::span<extensions::Scheduler*> schedulers,
                      std::shared_ptr<const node::NodeCapabilities> capabilities,
                      LocalReceiverRegistry* registry = nullptr);

  // Serializes and writes the kNodeAdvertisement frame to the connection. Must
  // be the first frame sent on every accepted gateway connection.
  void SendAdvertisement(io::Connection& conn);

  // Routes `frame` to the scheduler that declared its type_id in
  // HandledFrameTypes(); child-outcome frames route to the registry instead.
  // Frames with no owning scheduler are dropped with a warning.
  void HandleFrame(io::TlvFrame frame, io::Connection& conn);

private:
  void handleResultFrame(const io::TlvFrame& frame);
  void handleRejectedFrame(const io::TlvFrame& frame);

  std::unordered_map<uint8_t, extensions::Scheduler*> dispatcher_table_;
  std::shared_ptr<const node::NodeCapabilities> capabilities_;
  LocalReceiverRegistry* registry_;
};

} // namespace strij::nodeagent