#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace strij::io {

struct TlvFrame {
  uint8_t type_id;
  std::span<const std::byte> value;

  static constexpr uint8_t kTaskSubmission = 0;
  static constexpr uint8_t kResult = 1;
  static constexpr uint8_t kHeartbeat = 2;
  static constexpr uint8_t kNodeAdvertisement = 3;
  static constexpr uint8_t kNodeState = 4;
  static constexpr uint8_t kTaskRejected = 5;
  // Probe scheduling protocol (node self-selection against live capacity).
  // Direction is relative to the node: kTaskProbe, kTaskProbeCancel, and
  // kTaskGrant travel gateway → node; kTaskPull and kTaskDecline travel
  // node → gateway. Additive: ids 0–5 are untouched and unknown ids are
  // dropped by endpoints that don't implement the protocol.
  static constexpr uint8_t kTaskProbe = 6;
  static constexpr uint8_t kTaskProbeCancel = 7;
  static constexpr uint8_t kTaskPull = 8;
  static constexpr uint8_t kTaskGrant = 9;
  static constexpr uint8_t kTaskDecline = 10;
};

auto SerializeTlvFrame(uint8_t type_id, std::span<const std::byte> value)
    -> std::vector<std::byte>;

} // namespace strij::io
