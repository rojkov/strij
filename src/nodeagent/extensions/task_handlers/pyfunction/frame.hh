#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace strij::nodeagent::task_handlers::pyfunction {

// The wire protocol version carried by Hello/Ready.
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::size_t kLengthPrefixBytes = 4;
inline constexpr std::size_t kDefaultMaxFrameBytes = 8 * 1024 * 1024;

// Encodes `payload` as a 4-byte big-endian length followed by the payload.
// Fails with ResourceExhausted when the payload exceeds `max_frame_bytes`.
auto EncodeFrame(std::span<const std::byte> payload, std::size_t max_frame_bytes)
    -> absl::StatusOr<std::vector<std::byte>>;

// Incremental decoder for the length-prefixed stream. Append() buffers bytes;
// Next() yields one complete payload at a time. A declared length above the
// configured bound fails Append() immediately (even before the payload
// arrives), which the worker wrapper treats as a fatal protocol violation.
class FrameDecoder {
public:
  explicit FrameDecoder(std::size_t max_frame_bytes);
  ~FrameDecoder() = default;

  FrameDecoder(const FrameDecoder&) = delete;
  auto operator=(const FrameDecoder&) -> FrameDecoder& = delete;
  FrameDecoder(FrameDecoder&&) noexcept = delete;
  auto operator=(FrameDecoder&&) noexcept -> FrameDecoder& = delete;

  auto Append(std::span<const std::byte> data) -> absl::Status;

  // Returns the next complete payload, or an empty optional when more bytes are
  // needed.
  auto Next() -> std::optional<std::vector<std::byte>>;

  [[nodiscard]] auto MaxFrameBytes() const -> std::size_t { return max_frame_bytes_; }

private:
  std::size_t max_frame_bytes_;
  std::vector<std::byte> buffer_;
  std::size_t offset_{0};
};

} // namespace strij::nodeagent::task_handlers::pyfunction
