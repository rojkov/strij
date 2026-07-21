#include <arpa/inet.h>

#include <array>
#include <cstring>
#include <vector>

#include "core/io/tlv_frame.hh"
#include "core/io/tlv_parser.hh"
#include "gtest/gtest.h"

namespace carrot::io {
namespace {

auto make_wire_frame(uint8_t type, std::span<const std::byte> value) -> std::vector<std::byte> {
  std::vector<std::byte> frame;
  frame.push_back(std::byte{type});
  uint32_t len = htonl(static_cast<uint32_t>(value.size()));
  std::array<std::byte, 4> len_bytes{};
  std::memcpy(len_bytes.data(), &len, 4);
  frame.insert(frame.end(), len_bytes.begin(), len_bytes.end());
  frame.insert(frame.end(), value.begin(), value.end());
  return frame;
}

class TlvParserTest : public ::testing::Test {
protected:
  std::vector<TlvFrame> received_;
  TlvParser parser_{[this](TlvFrame frame) -> void { received_.push_back(frame); }};

  void feed(std::span<const std::byte> data) {
    size_t offset = 0;
    while (offset < data.size()) {
      auto buf = parser_.GetReadBuffer();
      size_t num_to_read_from_wire = std::min(data.size() - offset, buf.size());
      std::memcpy(buf.data(), std::next(data.data(), static_cast<ssize_t>(offset)),
                  num_to_read_from_wire);
      parser_.OnData(num_to_read_from_wire);
      offset += num_to_read_from_wire;
    }
  }
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(TlvParserTest, SingleCompleteFrame) {
  auto value = std::vector<std::byte>{std::byte{0xBB}, std::byte{0xCC}, std::byte{0xDD}};
  feed(make_wire_frame(0x01, value));
  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].type_id, 0x01);
  EXPECT_TRUE(std::equal(received_[0].value.begin(), received_[0].value.end(), value.begin()));
}

TEST_F(TlvParserTest, MultipleFramesInOneRead) {
  auto value1 = std::vector<std::byte>{std::byte{0xAA}, std::byte{0xBB}};
  auto value2 = std::vector<std::byte>{std::byte{0xCC}};
  auto frame1 = make_wire_frame(0x01, value1);
  auto frame2 = make_wire_frame(0x02, value2);

  std::vector<std::byte> combined;
  combined.insert(combined.end(), frame1.begin(), frame1.end());
  combined.insert(combined.end(), frame2.begin(), frame2.end());

  feed(combined);
  ASSERT_EQ(received_.size(), 2U);
  EXPECT_EQ(received_[0].type_id, 0x01);
  EXPECT_TRUE(
      std::equal(received_[0].value.begin(), received_[0].value.end(), value1.begin()));
  EXPECT_EQ(received_[1].type_id, 0x02);
  EXPECT_TRUE(
      std::equal(received_[1].value.begin(), received_[1].value.end(), value2.begin()));
}

TEST_F(TlvParserTest, ZeroLengthValue) {
  feed(make_wire_frame(0x01, std::span<const std::byte>{}));
  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].type_id, 0x01);
  EXPECT_TRUE(received_[0].value.empty());
}

TEST_F(TlvParserTest, LengthFieldSplitAcrossReads) {
  auto value = std::vector<std::byte>{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
  auto frame = make_wire_frame(0x01, value);

  // First read: type + 2 bytes of length
  feed(std::span<const std::byte>(frame.data(), 3));
  EXPECT_TRUE(received_.empty());

  // Second read: remaining 2 bytes of length + value
  feed(std::span<const std::byte>(std::next(frame.data(), 3), frame.size() - 3));
  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].type_id, 0x01);
  EXPECT_TRUE(
      std::equal(received_[0].value.begin(), received_[0].value.end(), value.begin()));
}

TEST_F(TlvParserTest, PartialLengthFieldThreeBytes) {
  auto value = std::vector<std::byte>{std::byte{0x77}, std::byte{0x88}};
  auto frame = make_wire_frame(0x01, value);

  // Send type + 3 bytes of length (need 4 but only have 3)
  feed(std::span<const std::byte>(frame.data(), 4));
  EXPECT_TRUE(received_.empty());

  // Send remaining 1 byte of length + value
  feed(std::span<const std::byte>(std::next(frame.data(), 4), frame.size() - 4));
  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].type_id, 0x01);
  EXPECT_TRUE(
      std::equal(received_[0].value.begin(), received_[0].value.end(), value.begin()));
}

TEST_F(TlvParserTest, LengthFieldByteByByte) {
  auto value = std::vector<std::byte>{std::byte{0x77}, std::byte{0x88}};
  auto frame = make_wire_frame(0x01, value);

  // Send type
  feed(std::span<const std::byte>(frame.data(), 1));
  EXPECT_TRUE(received_.empty());

  // Send length bytes one at a time (first 3)
  for (int i = 1; i <= 3; ++i) {
    feed(std::span<const std::byte>(std::next(frame.data(), i), 1));
    EXPECT_TRUE(received_.empty());
  }

  // Send 4th length byte + value
  feed(std::span<const std::byte>(std::next(frame.data(), 4), frame.size() - 4));
  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].type_id, 0x01);
  EXPECT_TRUE(
      std::equal(received_[0].value.begin(), received_[0].value.end(), value.begin()));
}

TEST_F(TlvParserTest, ValueSplitAcrossReads) {
  auto value = std::vector<std::byte>{std::byte{0x11}, std::byte{0x22}, std::byte{0x33},
                                      std::byte{0x44}, std::byte{0x55}};
  auto frame = make_wire_frame(0x01, value);

  // First read: header + 2 value bytes
  feed(std::span<const std::byte>(frame.data(), 7));
  EXPECT_TRUE(received_.empty());

  // Second read: remaining 3 value bytes
  feed(std::span<const std::byte>(std::next(frame.data(), 7), 3));
  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].type_id, 0x01);
  EXPECT_TRUE(
      std::equal(received_[0].value.begin(), received_[0].value.end(), value.begin()));
}

TEST_F(TlvParserTest, SingleByteValue) {
  auto value = std::vector<std::byte>{std::byte{0xFF}};
  auto frame = make_wire_frame(0x01, value);

  // Send only header (5 bytes), no value
  feed(std::span<const std::byte>(frame.data(), 5));
  EXPECT_TRUE(received_.empty());

  // Send value
  feed(std::span<const std::byte>(std::next(frame.data(), 5), 1));
  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].type_id, 0x01);
  EXPECT_TRUE(
      std::equal(received_[0].value.begin(), received_[0].value.end(), value.begin()));
}

TEST_F(TlvParserTest, FrameByteByByte) {
  auto value = std::vector<std::byte>{std::byte{0xEE}};
  auto frame = make_wire_frame(0x01, value);

  for (size_t i = 0; i < frame.size() - 1; ++i) {
    feed(std::span<const std::byte>(std::next(frame.data(), static_cast<ssize_t>(i)), 1));
    EXPECT_TRUE(received_.empty());
  }
  feed(std::span<const std::byte>(std::next(frame.data(), static_cast<ssize_t>(frame.size() - 1)),
                                  1));
  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].type_id, 0x01);
  EXPECT_TRUE(
      std::equal(received_[0].value.begin(), received_[0].value.end(), value.begin()));
}

TEST_F(TlvParserTest, BufferCompaction) {
  // First frame: fill most of the buffer so read_cursor_ is near the end
  // Frame: type(1) + length(4) + value(4089) = 4094 bytes
  // After processing: read_cursor_ = 4094
  auto big_value = std::vector<std::byte>(4089, std::byte{0xAA});
  auto frame1 = make_wire_frame(0x01, big_value);
  ASSERT_EQ(frame1.size(), 4094U);

  feed(frame1);
  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].type_id, 0x01);
  EXPECT_TRUE(
      std::equal(received_[0].value.begin(), received_[0].value.end(), big_value.begin()));

  // Second frame: send type + 1 byte of length (fills to buffer end)
  // This triggers compaction in type_read (read_cursor_ + 4 >= 4096)
  auto value2 = std::vector<std::byte>{std::byte{0xBB}};
  auto frame2 = make_wire_frame(0x02, value2);

  feed(std::span<const std::byte>(frame2.data(), 2));
  EXPECT_EQ(received_.size(), 1U);

  // Send remaining 3 bytes of length + value
  feed(std::span<const std::byte>(std::next(frame2.data(), 2), frame2.size() - 2));
  ASSERT_EQ(received_.size(), 2U);
  EXPECT_EQ(received_[1].type_id, 0x02);
  EXPECT_TRUE(
      std::equal(received_[1].value.begin(), received_[1].value.end(), value2.begin()));
}

TEST_F(TlvParserTest, ValueOverflowToVector) {
  // Value doesn't fit contiguously from read_cursor_ to end of buffer
  // Frame: type(1) + length(4) + value(4095)
  // After type_read: read_cursor_ = 5
  // read_cursor_ + 4095 = 4100 >= 4096 → overflow path
  auto value = std::vector<std::byte>(4095, std::byte{0xCC});
  auto frame = make_wire_frame(0x01, value);

  // Send header + 100 bytes of value
  feed(std::span<const std::byte>(frame.data(), 105));
  EXPECT_TRUE(received_.empty());

  // Send remaining value bytes
  feed(std::span<const std::byte>(std::next(frame.data(), 105), frame.size() - 105));
  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].type_id, 0x01);
  EXPECT_TRUE(
      std::equal(received_[0].value.begin(), received_[0].value.end(), value.begin()));
}

TEST_F(TlvParserTest, LargeValueMultipleReads) {
  // Value larger than buffer, requires multiple overflow rounds
  auto value = std::vector<std::byte>(8000, std::byte{0xDD});
  auto frame = make_wire_frame(0x01, value);

  // Send header + 100 bytes of value
  feed(std::span<const std::byte>(frame.data(), 105));
  EXPECT_TRUE(received_.empty());

  // Send next chunk
  feed(std::span<const std::byte>(std::next(frame.data(), 105), 4000));
  EXPECT_TRUE(received_.empty());

  // Send remaining bytes
  feed(std::span<const std::byte>(std::next(frame.data(), 4105), frame.size() - 4105));
  ASSERT_EQ(received_.size(), 1U);
  EXPECT_EQ(received_[0].type_id, 0x01);
  EXPECT_TRUE(
      std::equal(received_[0].value.begin(), received_[0].value.end(), value.begin()));
}

TEST_F(TlvParserTest, MultipleFramesByteByByte) {
  auto value1 = std::vector<std::byte>{std::byte{0x01}};
  auto value2 = std::vector<std::byte>{std::byte{0x02}};
  auto frame1 = make_wire_frame(0x10, value1);
  auto frame2 = make_wire_frame(0x20, value2);

  std::vector<std::byte> all;
  all.insert(all.end(), frame1.begin(), frame1.end());
  all.insert(all.end(), frame2.begin(), frame2.end());

  for (size_t i = 0; i < all.size() - 1; ++i) {
    feed(std::span<const std::byte>(std::next(all.data(), static_cast<ssize_t>(i)), 1));
  }
  feed(std::span<const std::byte>(std::next(all.data(), static_cast<ssize_t>(all.size() - 1)), 1));
  ASSERT_EQ(received_.size(), 2U);
  EXPECT_EQ(received_[0].type_id, 0x10);
  EXPECT_TRUE(
      std::equal(received_[0].value.begin(), received_[0].value.end(), value1.begin()));
  EXPECT_EQ(received_[1].type_id, 0x20);
  EXPECT_TRUE(
      std::equal(received_[1].value.begin(), received_[1].value.end(), value2.begin()));
}
// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace carrot::io
