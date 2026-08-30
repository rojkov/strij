#pragma once

#include "gateway/core/exact_state_tracker.hh"
#include "gateway/core/node_directory.hh"
#include "gateway/core/result_receiver_storage.hh"
#include "common/core/io/connection.hh"
#include "common/core/io/tlv_frame.hh"

namespace strij::extensions {

class Scheduler;

}

namespace strij::gateway {

// The capability version this gateway understands. v1 mismatch is a warning,
// not a rejection.
inline constexpr uint32_t kSupportedCapabilityVersion = 1;

class GatewayTlvHandler final {
public:
  GatewayTlvHandler(NodeDirectoryImpl& directory, ResultReceiverStorage& storage,
                    // TODO: I don't like default parameters, they make code error prone.
                    extensions::Scheduler* scheduler = nullptr,
                    ExactStateTracker* state_tracker = nullptr)
      : directory_{directory}, storage_{storage}, scheduler_{scheduler},
        state_tracker_{state_tracker} {}
  ~GatewayTlvHandler() = default;

  GatewayTlvHandler(const GatewayTlvHandler&) = delete;
  auto operator=(const GatewayTlvHandler&) -> GatewayTlvHandler& = delete;
  GatewayTlvHandler(GatewayTlvHandler&&) noexcept = delete;
  auto operator=(GatewayTlvHandler&&) noexcept -> GatewayTlvHandler& = delete;

  auto HandleFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status;

private:
  // Returns the Node that owns the connection, or nullptr if the frame
  // arrived on a connection not owned by a gateway Node.
  static auto owningNode(io::Connection& conn) -> Node*;

  auto handleNodeAdvertisementFrame(const io::TlvFrame& frame, io::Connection& conn)
      -> absl::Status;
  auto handleNodeStateFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status;
  auto handleTaskRejectedFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status;
  auto handleTaskResultFrame(const io::TlvFrame& frame, io::Connection& conn) -> absl::Status;

  NodeDirectoryImpl& directory_;
  ResultReceiverStorage& storage_;
  // Optional inbound-frame routing seam: a frame type not owned by a built-in
  // handler is handed to the scheduler, which routes it (and reports via its
  // return Status whether it was claimed). Null when no router is installed
  // (unit tests).
  extensions::Scheduler* scheduler_;
  // Optional exact state accounting; null in unit tests that don't need it.
  ExactStateTracker* state_tracker_;
};

} // namespace strij::gateway
