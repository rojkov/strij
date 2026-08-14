#pragma once

#include "core/gateway/exact_state_tracker.hh"
#include "core/gateway/node_directory.hh"
#include "core/gateway/result_receiver_storage.hh"
#include "core/io/connection.hh"
#include "core/io/tlv_frame.hh"

namespace strij::gateway {

// The capability version this gateway understands. v1 mismatch is a warning,
// not a rejection.
inline constexpr uint32_t kSupportedCapabilityVersion = 1;

class GatewayTlvHandler final {
public:
  GatewayTlvHandler(NodeDirectory& directory, ResultReceiverStorage& storage,
                    ExactStateTracker* state_tracker = nullptr)
      : directory_{directory}, storage_{storage}, state_tracker_{state_tracker} {}
  ~GatewayTlvHandler() = default;

  GatewayTlvHandler(const GatewayTlvHandler&) = delete;
  auto operator=(const GatewayTlvHandler&) -> GatewayTlvHandler& = delete;
  GatewayTlvHandler(GatewayTlvHandler&&) noexcept = delete;
  auto operator=(GatewayTlvHandler&&) noexcept -> GatewayTlvHandler& = delete;

  void HandleFrame(strij::io::TlvFrame frame, strij::io::Connection& conn);

private:
  // Returns the Node that owns the connection, or nullptr if the frame
  // arrived on a connection not owned by a gateway Node.
  auto owningNode(strij::io::Connection& conn) -> Node*;

  NodeDirectory& directory_;
  ResultReceiverStorage& storage_;
  // Optional exact state accounting; null in unit tests that don't need it.
  ExactStateTracker* state_tracker_;
};

} // namespace strij::gateway
