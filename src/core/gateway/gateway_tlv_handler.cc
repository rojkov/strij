#include "core/gateway/gateway_tlv_handler.hh"

#include <cstddef>
#include <span>
#include <utility>

#include "src/core/gateway/gateway_tlv_handler.hh"

#include "core/io/tlv_frame.hh"
#include "core/logging/log.hh"
#include "core/node/capabilities.pb.h"
#include "core/task/task.pb.h"

namespace strij::gateway {

auto GatewayTlvHandler::owningNode(io::Connection& conn) -> Node* {
  return dynamic_cast<Node*>(conn.GetOwner());
}

auto GatewayTlvHandler::HandleFrame(const io::TlvFrame& frame, io::Connection& conn)
    -> absl::Status {
  switch (frame.type_id) {
  case io::TlvFrame::kNodeAdvertisement:
    return handleNodeAdvertisementFrame(frame, conn);
  case io::TlvFrame::kNodeState:
    return handleNodeStateFrame(frame, conn);
  case io::TlvFrame::kTaskRejected:
    return handleTaskRejectedFrame(frame, conn);
  case io::TlvFrame::kResult:
    return handleTaskResultFrame(frame, conn);
  case io::TlvFrame::kHeartbeat:
    LOG_DEBUG("Received heartbeat");
    return absl::OkStatus();
  default:
    return absl::InvalidArgumentError(std::format("Unknown TLV type_id: {}", frame.type_id));
  }
}

auto GatewayTlvHandler::handleNodeAdvertisementFrame(const io::TlvFrame& frame,
                                                     io::Connection& conn) -> absl::Status {
  node::NodeCapabilities capabilities;
  if (!capabilities.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                                   static_cast<int>(frame.value.size()))) {
    return absl::InvalidArgumentError("malformed node::NodeCapabilities message");
  }

  Node* node = owningNode(conn);
  if (node == nullptr) {
    return absl::InvalidArgumentError(
        "NodeCapabilities frame on a connection without an owning Node");
  }

  if (capabilities.capability_version() != kSupportedCapabilityVersion) {
    LOG_WARNING("Node {} advertises capability_version {} (supported {}); continuing",
                node->GetNodeId(), capabilities.capability_version(), kSupportedCapabilityVersion);
  }

  const std::string advertised_node_id = capabilities.node_id();
  node->StoreCapabilities(std::move(capabilities));
  LOG_INFO("Node {} advertised capabilities (node_id={})", node->GetNodeId(), advertised_node_id);
  if (advertised_node_id != node->GetNodeId()) {
    LOG_INFO("Rekeying node record {} -> {}", node->GetNodeId(), advertised_node_id);
    directory_.RekeyNode(node->GetNodeId(), advertised_node_id);
  }

  return absl::OkStatus();
}

auto GatewayTlvHandler::handleNodeStateFrame(const io::TlvFrame& frame, io::Connection& conn)
    -> absl::Status {
  node::NodeState state;
  if (!state.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                            static_cast<int>(frame.value.size()))) {
    return absl::InvalidArgumentError("malformed node::NodeState message");
  }

  Node* node = owningNode(conn);
  if (node == nullptr) {
    return absl::InternalError("NodeState frame on a connection without an owning Node");
  }

  if (state_tracker_ != nullptr) {
    state_tracker_->ApplyStateSnapshot(state);
  }

  node->UpdateState(std::move(state));

  return absl::OkStatus();
}

auto GatewayTlvHandler::handleTaskRejectedFrame(const io::TlvFrame& frame, io::Connection& /*conn*/)
    -> absl::Status {
  task::TaskRejected rejected;
  if (!rejected.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                               static_cast<int>(frame.value.size()))) {
    return absl::InvalidArgumentError("malformed task::TaskRejected message");
  }

  auto* receiver = storage_.Get(rejected.id());
  if (receiver == nullptr) {
    return absl::InternalError(std::format("No receiver for rejected task {}", rejected.id()));
  }

  receiver->DeliverError(rejected.reason());
  storage_.Erase(rejected.id());
  if (state_tracker_ != nullptr) {
    state_tracker_->RecordCompletion(rejected.id());
  }
  LOG_WARNING("Task {} rejected by node: {}", rejected.id(), rejected.reason());

  return absl::OkStatus();
}

auto GatewayTlvHandler::handleTaskResultFrame(const io::TlvFrame& frame, io::Connection& /*conn*/)
    -> absl::Status {

  task::TaskResult result;
  if (!result.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                             static_cast<int>(frame.value.size()))) {
    return absl::InvalidArgumentError("malformed task::TaskResult message");
  }

  auto* receiver = storage_.Get(result.id());
  if (receiver == nullptr) {
    return absl::InternalError(std::format("No receiver for task {}", result.id()));
  }

  const bool is_final = !result.has_is_final() || result.is_final();
  const auto* body_ptr = std::bit_cast<const std::byte*>(result.body().data());
  auto body = std::span<const std::byte>(body_ptr, result.body().size());
  receiver->Deliver(body, is_final);

  if (is_final) {
    storage_.Erase(result.id());

    if (state_tracker_ != nullptr) {
      state_tracker_->RecordCompletion(result.id());
    }

    LOG_DEBUG("Delivered final result for task {}", result.id());
  } else {
    LOG_DEBUG("Delivered intermediate result for task {}", result.id());
  }

  return absl::OkStatus();
}

} // namespace strij::gateway
