#pragma once

#include <cstdint>
#include <functional>

#include "core/io/protocol_parser.hh"
#include "core/io/tlv_frame.hh"

namespace strij::io {

const size_t kBufferSize{4096};

class TlvParser final : public ProtocolParser {
public:
  explicit TlvParser(std::move_only_function<void(TlvFrame)>&& on_message);

  // ProtocolParser interface
  auto GetReadBuffer() -> std::span<std::byte> override;
  auto OnData(size_t bytes_read) -> Action override;

private:
  enum state : uint8_t { empty = 1, type_read, length_read, value_partially_copied };
  state state_{empty};

  struct frame {
    uint8_t type_id_;
    uint32_t length_;
  };

  auto iterateThroughReadBuffer() -> bool;
  void setState(state new_state);

  frame frame_{.type_id_ = 0, .length_ = 0};
  std::move_only_function<void(TlvFrame)> on_message_;
  std::array<std::byte, kBufferSize> buffer_{};
  size_t cursor_{0};
  size_t bytes_not_parsed_{0};
  std::unique_ptr<std::vector<std::byte>> accumulated_value_{nullptr};
};

} // namespace strij::io
