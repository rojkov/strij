#include "src/core/io/tlv_parser.hh"

#include <arpa/inet.h>
#include <sys/types.h>

#include <cstdint>

#include "core/logging/log.hh"

namespace strij::io {

TlvParser::TlvParser(std::move_only_function<void(TlvFrame)>&& on_message)
    : on_message_{std::move(on_message)} {}

auto TlvParser::GetReadBuffer() -> std::span<std::byte> {
  assert(cursor_ <= buffer_.size());

  if (cursor_ == buffer_.size()) {
    cursor_ = 0;
  }

  switch (state_) {
  case empty: {
    return {std::next(buffer_.data(), static_cast<ssize_t>(cursor_)), buffer_.size() - cursor_};
  }
  case type_read: {
    assert((cursor_ + bytes_not_parsed_) < buffer_.size());
    return {std::next(buffer_.data(), static_cast<ssize_t>(cursor_ + bytes_not_parsed_)),
            buffer_.size() - (cursor_ + bytes_not_parsed_)};
  }
  case length_read: {
    assert((cursor_ + bytes_not_parsed_) < buffer_.size());
    return {std::next(buffer_.data(), static_cast<ssize_t>(cursor_ + bytes_not_parsed_)),
            buffer_.size() - (cursor_ + bytes_not_parsed_)};
  }
  case value_partially_copied: {
    return {std::next(buffer_.data(), static_cast<ssize_t>(cursor_)), buffer_.size() - cursor_};
  }
  }
}

auto TlvParser::OnData(size_t bytes_read) -> Action {
  LOG_DEBUG("TlvParser::OnData");

  bytes_not_parsed_ += bytes_read;

  while (bytes_not_parsed_ > 0 && iterateThroughReadBuffer()) {
  }

  return Action::NeedMoreData;
}

auto TlvParser::iterateThroughReadBuffer() -> bool {
  switch (state_) {
  case empty: {
    frame_.type_id_ = std::bit_cast<uint8_t>(buffer_.at(cursor_));
    setState(type_read);
    cursor_ += sizeof(uint8_t);
    bytes_not_parsed_ -= sizeof(uint8_t);
    break;
  }

  case type_read: {
    if ((cursor_ + sizeof(frame_.length_)) >= buffer_.size()) {
      assert(bytes_not_parsed_ < sizeof(frame_.length_));

      for (size_t i = 0; i < bytes_not_parsed_; i++) {
        buffer_.at(i) = buffer_.at(cursor_ + i);
      }

      cursor_ = 0;

      return false; // need more data
    }

    if (bytes_not_parsed_ < sizeof(frame_.length_)) {
      return false; // need more data
    }

    frame_.length_ = ntohl(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        *reinterpret_cast<uint32_t*>(std::next(buffer_.data(), static_cast<ssize_t>(cursor_))));
    cursor_ += sizeof(uint32_t);
    bytes_not_parsed_ -= sizeof(uint32_t);
    LOG_DEBUG("getting value of length {}", frame_.length_);

    if (frame_.length_ == 0) {
      setState(empty);
    } else {
      setState(length_read);
    }

    break;
  }

  case length_read: {
    if (bytes_not_parsed_ >= frame_.length_) {
      bytes_not_parsed_ -= frame_.length_;
      setState(empty);
      cursor_ += frame_.length_;
      break;
    }

    if ((cursor_ + frame_.length_) < buffer_.size()) {
      return false; // need more data
    }

    accumulated_value_ = std::make_unique<std::vector<std::byte>>();
    accumulated_value_->reserve(frame_.length_);
    accumulated_value_->insert_range(
        accumulated_value_->begin(),
        std::span<std::byte>(std::next(buffer_.data(), static_cast<ssize_t>(cursor_)),
                             bytes_not_parsed_));
    cursor_ += bytes_not_parsed_;
    bytes_not_parsed_ = 0;
    setState(value_partially_copied);

    return false; // need more data
  }

  case value_partially_copied: {
    if ((accumulated_value_->size() + bytes_not_parsed_) >= frame_.length_) {
      auto accumulated_size = accumulated_value_->size();
      accumulated_value_->insert_range(
          accumulated_value_->end(),
          std::span<std::byte>(std::next(buffer_.data(), static_cast<ssize_t>(cursor_)),
                               frame_.length_ - accumulated_size));
      cursor_ += (frame_.length_ - accumulated_size);
      bytes_not_parsed_ -= (frame_.length_ - accumulated_size);
      setState(empty);
      break;
    }

    accumulated_value_->insert_range(
        accumulated_value_->end(),
        std::span<std::byte>(std::next(buffer_.data(), static_cast<ssize_t>(cursor_)),
                             bytes_not_parsed_));
    cursor_ += bytes_not_parsed_;
    bytes_not_parsed_ = 0;

    return false; // need more data
  }
  }

  return true;
}

void TlvParser::setState(state new_state) {
  switch (new_state) {
  case empty: {
    assert(state_ == type_read || state_ == length_read || state_ == value_partially_copied);
    if (state_ == type_read) {
      // Zero-length value: deliver TlvFrame with empty value
      on_message_(TlvFrame{frame_.type_id_, std::span<const std::byte>{}});
    } else if (state_ == length_read) {
      // Value fits in buffer: deliver TlvFrame with span over buffer
      auto value = std::span<const std::byte>(
          std::next(buffer_.data(), static_cast<ssize_t>(cursor_)), frame_.length_);
      on_message_(TlvFrame{frame_.type_id_, value});
    } else if (state_ == value_partially_copied) {
      // Value was accumulated in a vector: deliver TlvFrame with span over accumulated
      assert(accumulated_value_ != nullptr);
      on_message_(
          TlvFrame{frame_.type_id_, std::span<const std::byte>(accumulated_value_->data(),
                                                               accumulated_value_->size())});
      accumulated_value_.reset(nullptr);
    }

    break;
  }
  case type_read:
    assert(state_ == empty);
    break;
  case length_read:
    assert(state_ == type_read);
    break;
  case value_partially_copied:
    assert(state_ == length_read);
    assert(accumulated_value_ != nullptr);
    break;
  }

  state_ = new_state;
}

} // namespace strij::io