#include "nodeagent/core/nodeagent_tlv_handler.hh"

#include <bit>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/logging/log.hh"
#include "common/node/capabilities.pb.h"
#include "common/task/task.pb.h"
#include "strij/extensions/scheduler.hh"

namespace strij::nodeagent {

NodeagentTlvHandler::NodeagentTlvHandler(
    std::span<extensions::Scheduler*> schedulers,
    std::shared_ptr<const node::NodeCapabilities> capabilities, LocalReceiverRegistry* registry)
    : capabilities_{std::move(capabilities)}, registry_{registry} {
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

void NodeagentTlvHandler::handleResultFrame(const io::TlvFrame& frame) {
  if (registry_ == nullptr) {
    LOG_WARNING("Result frame dropped: no local receiver registry installed");
    return;
  }

  task::TaskResult result;
  if (!result.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                             static_cast<int>(frame.value.size()))) {
    LOG_WARNING("Malformed TaskResult frame dropped");
    return;
  }

  gateway::ResultReceiver* receiver = registry_->Get(result.id());
  if (receiver == nullptr) {
    LOG_WARNING("Result for unknown child task '{}' dropped", result.id());
    return;
  }

  const bool is_final = !result.has_is_final() || result.is_final();
  const std::string& body = result.body();
  const auto data = std::as_bytes(std::span(body.data(), body.size()));
  receiver->Deliver(data, is_final);
  if (is_final) {
    registry_->Erase(result.id());
  }
}

void NodeagentTlvHandler::handleRejectedFrame(const io::TlvFrame& frame) {
  if (registry_ == nullptr) {
    LOG_WARNING("Rejection frame dropped: no local receiver registry installed");
    return;
  }

  task::TaskRejected rejected;
  if (!rejected.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                               static_cast<int>(frame.value.size()))) {
    LOG_WARNING("Malformed TaskRejected frame dropped");
    return;
  }

  gateway::ResultReceiver* receiver = registry_->Get(rejected.id());
  if (receiver == nullptr) {
    LOG_WARNING("Rejection for unknown child task '{}' dropped", rejected.id());
    return;
  }

  receiver->DeliverError(rejected.reason());
  registry_->Erase(rejected.id());
}

void NodeagentTlvHandler::HandleFrame(io::TlvFrame frame, io::Connection& conn) {
  // Core-owned child-outcome frames: they resolve receivers in the local
  // registry and belong to no scheduling protocol, so they are handled before
  // the dispatcher-table lookup.
  if (frame.type_id == io::TlvFrame::kResult) {
    handleResultFrame(frame);
    return;
  }
  if (frame.type_id == io::TlvFrame::kTaskRejected) {
    handleRejectedFrame(frame);
    return;
  }

  auto iter = dispatcher_table_.find(frame.type_id);
  if (iter == dispatcher_table_.end()) {
    LOG_WARNING("No scheduler owns frame type {}", static_cast<int>(frame.type_id));
    return;
  }

  const absl::Status status = iter->second->HandleFrame(frame, conn);
  if (!status.ok()) {
    LOG_WARNING("Scheduler dropped frame type {}: {}", static_cast<int>(frame.type_id),
                status.message());
  }
}

} // namespace strij::nodeagent