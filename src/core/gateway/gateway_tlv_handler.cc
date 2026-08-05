#include "core/gateway/gateway_tlv_handler.hh"

#include <cstddef>
#include <span>

#include "core/io/tlv_frame.hh"
#include "core/logging/log.hh"
#include "core/task/task.pb.h"

namespace strij::gateway {

void GatewayTlvHandler::HandleFrame(io::TlvFrame frame, io::Connection& /*conn*/) {
  switch (frame.type_id) {
  case io::TlvFrame::kResult: {
    task::TaskResult result;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (!result.ParseFromArray(reinterpret_cast<const char*>(frame.value.data()),
                               static_cast<int>(frame.value.size()))) {
      LOG_WARNING("Malformed TaskResult frame dropped");
      return;
    }

    auto* receiver = storage_.get(result.id());
    if (receiver != nullptr) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      const auto* body_ptr = reinterpret_cast<const std::byte*>(result.body().data());
      auto body = std::span<const std::byte>(body_ptr, result.body().size());
      receiver->Deliver(body);
      storage_.erase(result.id());
      LOG_DEBUG("Delivered result for task {}", result.id());
    } else {
      LOG_WARNING("No receiver for task {}", result.id());
    }
    break;
  }
  case io::TlvFrame::kHeartbeat: {
    LOG_DEBUG("Received heartbeat");
    break;
  }
  default:
    LOG_WARNING("Unknown TLV type_id: {}", frame.type_id);
    break;
  }
}

} // namespace strij::gateway
