#include "nodeagent/extensions/task_handlers/pyfunction/frame.hh"

#include <algorithm>
#include <optional>

#include "absl/status/status.h"

namespace strij::nodeagent::task_handlers::pyfunction {
namespace {

auto readBigEndian32(const std::byte* data) -> std::uint32_t {
  return (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[0])) << 24U) |
         (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[1])) << 16U) |
         (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[2])) << 8U) |
         static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[3]));
}

} // namespace

auto EncodeFrame(std::span<const std::byte> payload, std::size_t max_frame_bytes)
    -> absl::StatusOr<std::vector<std::byte>> {
  if (payload.size() > max_frame_bytes) {
    return absl::ResourceExhaustedError("frame exceeds the configured maximum");
  }

  std::vector<std::byte> frame;
  frame.resize(kLengthPrefixBytes + payload.size());
  const auto length = static_cast<std::uint32_t>(payload.size());
  frame[0] = static_cast<std::byte>((length >> 24U) & 0xFFU);
  frame[1] = static_cast<std::byte>((length >> 16U) & 0xFFU);
  frame[2] = static_cast<std::byte>((length >> 8U) & 0xFFU);
  frame[3] = static_cast<std::byte>(length & 0xFFU);

  if (!payload.empty()) {
    std::copy(payload.begin(), payload.end(), frame.begin() + kLengthPrefixBytes);
  }

  return frame;
}

FrameDecoder::FrameDecoder(std::size_t max_frame_bytes) : max_frame_bytes_{max_frame_bytes} {}

auto FrameDecoder::Append(std::span<const std::byte> data) -> absl::Status {
  buffer_.insert(buffer_.end(), data.begin(), data.end());

  // Validate the declared length as soon as the prefix is available.
  if (buffer_.size() - offset_ >= kLengthPrefixBytes) {
    const std::uint32_t declared =
        readBigEndian32(buffer_.data() + static_cast<std::ptrdiff_t>(offset_));

    if (declared > max_frame_bytes_) {
      return absl::ResourceExhaustedError("frame length exceeds the configured maximum");
    }
  }

  return absl::OkStatus();
}

auto FrameDecoder::Next() -> std::optional<std::vector<std::byte>> {
  if (buffer_.size() - offset_ < kLengthPrefixBytes) {
    return std::nullopt;
  }

  const std::uint32_t declared =
      readBigEndian32(buffer_.data() + static_cast<std::ptrdiff_t>(offset_));

  if (buffer_.size() - offset_ < kLengthPrefixBytes + declared) {
    return std::nullopt;
  }

  const auto payload_begin =
      buffer_.begin() + static_cast<std::ptrdiff_t>(offset_ + kLengthPrefixBytes);
  std::vector<std::byte> payload(payload_begin,
                                 payload_begin + static_cast<std::ptrdiff_t>(declared));
  offset_ += kLengthPrefixBytes + declared;

  // Reclaim consumed prefix space once it dominates the buffer.
  if (offset_ > 64 * 1024 && offset_ * 2 > buffer_.size()) {
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset_));
    offset_ = 0;
  }

  return payload;
}

} // namespace strij::nodeagent::task_handlers::pyfunction
