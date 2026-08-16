#include "core/nodeagent/state_reporter.hh"

#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/io/outbound_mailbox.hh"
#include "core/io/tlv_frame.hh"

namespace strij::nodeagent {

StateReporter::StateReporter(std::shared_ptr<AdmissionController> controller, std::string node_id)
    : controller_{std::move(controller)}, node_id_{std::move(node_id)} {}

void StateReporter::AddConnection(std::shared_ptr<io::OutboundMailbox> mailbox) {
  auto self = shared_from_this();
  std::size_t token =
      mailbox->RegisterOnClose([self, mailbox]() -> void { self->removeConnection(mailbox); });
  connections_.emplace_back(token, std::move(mailbox));
}

void StateReporter::removeConnection(const std::shared_ptr<io::OutboundMailbox>& mailbox) {
  for (auto iter = connections_.begin(); iter != connections_.end(); ++iter) {
    if (iter->second == mailbox) {
      connections_.erase(iter);

      return;
    }
  }
}

void StateReporter::Broadcast() {
  if (connections_.empty()) {
    return;
  }

  const auto state = controller_->BuildStateSnapshot(node_id_, ++seq_);
  std::string serialized;
  state.SerializeToString(&serialized);
  const auto frame = io::SerializeTlvFrame(
      io::TlvFrame::kNodeState, std::as_bytes(std::span(serialized.data(), serialized.size())));

  for (const auto& [token, mailbox] : connections_) {
    (void)token;
    mailbox->Enqueue(frame);
  }
}

} // namespace strij::nodeagent
