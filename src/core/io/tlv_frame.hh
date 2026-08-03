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
};

auto SerializeTlvFrame(uint8_t type_id, std::span<const std::byte> value)
    -> std::vector<std::byte>;

} // namespace strij::io
