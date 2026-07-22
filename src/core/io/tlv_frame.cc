#include "tlv_frame.hh"

#include <arpa/inet.h>

#include <cstring>

namespace carrot::io {

auto SerializeTlvFrame(uint8_t type_id, std::span<const std::byte> value)
    -> std::vector<std::byte> {
  uint32_t length = static_cast<uint32_t>(value.size());
  uint32_t net_length = htonl(length);

  std::vector<std::byte> frame;
  frame.reserve(1 + sizeof(uint32_t) + value.size());

  frame.push_back(std::bit_cast<std::byte>(type_id));

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* len_bytes = reinterpret_cast<const std::byte*>(&net_length);
  frame.insert(frame.end(), len_bytes, len_bytes + sizeof(uint32_t));

  frame.insert(frame.end(), value.begin(), value.end());

  return frame;
}

} // namespace carrot::io
