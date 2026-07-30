#include "core/gateway/gateway_tlv_handler.hh"

#include <cstring>

#include "core/io/tlv_frame.hh"
#include "core/logging/log.hh"

namespace carrot::gateway {

void GatewayTlvHandler::HandleFrame(carrot::io::TlvFrame frame, carrot::io::Connection& /*conn*/) {
  switch (frame.type_id) {
  case carrot::io::TlvFrame::kResult: {
    if (frame.value.size() < sizeof(uint64_t)) {
      LOG_WARNING("Result frame with undersized value");
      return;
    }

    uint64_t task_id{};
    std::memcpy(&task_id, frame.value.data(), sizeof(uint64_t));
    auto payload = frame.value.subspan(sizeof(uint64_t));

    auto* receiver = storage_.get(task_id);
    if (receiver) {
      receiver->Deliver(payload);
      storage_.erase(task_id);
      LOG_DEBUG("Delivered result for task {}", task_id);
    } else {
      LOG_WARNING("No receiver for task {}", task_id);
    }
    break;
  }
  case carrot::io::TlvFrame::kHeartbeat: {
    LOG_DEBUG("Received heartbeat");
    break;
  }
  default:
    LOG_WARNING("Unknown TLV type_id: {}", frame.type_id);
    break;
  }
}

} // namespace carrot::gateway
