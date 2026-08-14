#include "core/gateway/gateway_tlv_handler.hh"

#include <cstddef>
#include <span>
#include <utility>

#include "core/io/tlv_frame.hh"
#include "core/logging/log.hh"
#include "core/node/capabilities.pb.h"
#include "core/task/task.pb.h"

namespace strij::gateway {

auto GatewayTlvHandler::owningNode(io::Connection& conn) -> Node* {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return dynamic_cast<Node*>(conn.GetOwner());
}

void GatewayTlvHandler::HandleFrame(io::TlvFrame frame, io::Connection& conn) {
  switch (frame.type_id) {
  case io::TlvFrame::kNodeAdvertisement: {
    strij::node::NodeCapabilities capabilities;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (!capabilities.ParseFromArray(reinterpret_cast<const char*>(frame.value.data()),
                                     static_cast<int>(frame.value.size()))) {
      LOG_WARNING("Malformed NodeCapabilities frame dropped");
      return;
    }

    Node* node = owningNode(conn);
    if (node == nullptr) {
      LOG_WARNING("NodeCapabilities frame on a connection without an owning Node; dropped");
      return;
    }

    if (capabilities.capability_version() != kSupportedCapabilityVersion) {
      LOG_WARNING("Node {} advertises capability_version {} (supported {}); continuing",
                  node->GetNodeId(), capabilities.capability_version(),
                  kSupportedCapabilityVersion);
    }

    const std::string advertised_node_id = capabilities.node_id();
    node->StoreCapabilities(std::move(capabilities));
    LOG_INFO("Node {} advertised capabilities (node_id={})", node->GetNodeId(),
             advertised_node_id);
    if (advertised_node_id != node->GetNodeId()) {
      LOG_INFO("Rekeying node record {} -> {}", node->GetNodeId(), advertised_node_id);
      directory_.RekeyNode(node->GetNodeId(), advertised_node_id);
    }
    break;
  }
  case io::TlvFrame::kNodeState: {
    strij::node::NodeState state;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (!state.ParseFromArray(reinterpret_cast<const char*>(frame.value.data()),
                              static_cast<int>(frame.value.size()))) {
      LOG_WARNING("Malformed NodeState frame dropped");
      return;
    }

    Node* node = owningNode(conn);
    if (node == nullptr) {
      LOG_WARNING("NodeState frame on a connection without an owning Node; dropped");
      return;
    }
    if (state_tracker_ != nullptr) {
      state_tracker_->ApplyStateSnapshot(state);
    }
    node->UpdateState(std::move(state));
    break;
  }
  case io::TlvFrame::kTaskRejected: {
    task::TaskRejected rejected;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (!rejected.ParseFromArray(reinterpret_cast<const char*>(frame.value.data()),
                                 static_cast<int>(frame.value.size()))) {
      LOG_WARNING("Malformed TaskRejected frame dropped");
      return;
    }

    auto* receiver = storage_.get(rejected.id());
    if (receiver != nullptr) {
      receiver->DeliverError(rejected.reason());
      storage_.erase(rejected.id());
      if (state_tracker_ != nullptr) {
        state_tracker_->RecordCompletion(rejected.id());
      }
      LOG_WARNING("Task {} rejected by node: {}", rejected.id(), rejected.reason());
    } else {
      LOG_WARNING("No receiver for rejected task {}", rejected.id());
    }
    break;
  }
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
      const bool is_final = !result.has_is_final() || result.is_final();
      const auto* body_ptr = std::bit_cast<const std::byte*>(result.body().data());
      auto body = std::span<const std::byte>(body_ptr, result.body().size());
      receiver->Deliver(body, is_final);
      if (is_final) {
        storage_.erase(result.id());
        if (state_tracker_ != nullptr) {
          state_tracker_->RecordCompletion(result.id());
        }
        LOG_DEBUG("Delivered final result for task {}", result.id());
      } else {
        LOG_DEBUG("Delivered intermediate result for task {}", result.id());
      }
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
