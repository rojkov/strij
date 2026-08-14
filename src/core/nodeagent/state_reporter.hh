#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/io/outbound_mailbox.hh"
#include "core/io/tlv_frame.hh"
#include "core/nodeagent/admission_controller.hh"

namespace strij::nodeagent {

// Broadcasts kNodeState snapshots (built from the AdmissionController) to every
// registered node connection. Connections register via their OutboundMailbox;
// the entry is removed automatically when the connection is torn down. Sends to
// a closed mailbox are no-ops, so Broadcast() is safe even with stale handles.
// The close callback holds a shared_ptr to the reporter (via
// shared_from_this), so the reporter stays alive as long as any connection it
// registered with is still live.
class StateReporter final : public std::enable_shared_from_this<StateReporter> {
public:
  StateReporter(std::shared_ptr<AdmissionController> controller, std::string node_id);

  void AddConnection(std::shared_ptr<io::OutboundMailbox> mailbox);

  // Builds the next kNodeState snapshot and enqueues it as a TLV frame on every
  // registered connection.
  void Broadcast();

private:
  void removeConnection(const std::shared_ptr<io::OutboundMailbox>& mailbox);

  std::shared_ptr<AdmissionController> controller_;
  std::string node_id_;
  uint64_t seq_{0};
  std::vector<std::pair<std::size_t, std::shared_ptr<io::OutboundMailbox>>> connections_;
};

} // namespace strij::nodeagent
