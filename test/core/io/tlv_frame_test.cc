#include <arpa/inet.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "core/io/tlv_frame.hh"
#include "core/io/tlv_parser.hh"
#include "core/task/task.pb.h"
#include "gtest/gtest.h"

namespace carrot::io {
namespace {

class SerializeTlvFrameTest : public ::testing::Test {};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(SerializeTlvFrameTest, SerializesCorrectWireFormat) {
  auto value = std::vector<std::byte>{std::byte{0xAA}, std::byte{0xBB}};
  auto frame = SerializeTlvFrame(TlvFrame::kTaskSubmission, value);

  // [type_id:1][length:4][value:N]
  ASSERT_GE(frame.size(), 5U);
  EXPECT_EQ(static_cast<uint8_t>(frame[0]), TlvFrame::kTaskSubmission);

  uint32_t net_len{};
  std::memcpy(&net_len, frame.data() + 1, 4);
  uint32_t length = ntohl(net_len);
  EXPECT_EQ(length, value.size());

  EXPECT_TRUE(std::equal(frame.begin() + 5, frame.begin() + 5 + value.size(), value.begin()));
}

TEST_F(SerializeTlvFrameTest, SerializesResultType) {
  auto value = std::vector<std::byte>{std::byte{0x11}};
  auto frame = SerializeTlvFrame(TlvFrame::kResult, value);

  ASSERT_GE(frame.size(), 5U);
  EXPECT_EQ(static_cast<uint8_t>(frame[0]), TlvFrame::kResult);
}

TEST_F(SerializeTlvFrameTest, SerializesEmptyValue) {
  auto frame = SerializeTlvFrame(TlvFrame::kHeartbeat, std::span<const std::byte>{});

  ASSERT_GE(frame.size(), 5U);
  EXPECT_EQ(static_cast<uint8_t>(frame[0]), TlvFrame::kHeartbeat);

  uint32_t net_len{};
  std::memcpy(&net_len, frame.data() + 1, 4);
  uint32_t length = ntohl(net_len);
  EXPECT_EQ(length, 0U);
}

TEST_F(SerializeTlvFrameTest, RoundTripsSerializedTaskThroughTlvParser) {
  carrot::task::Task task;
  task.set_id("42");
  task.set_type("echo");
  task.set_body("hello");
  std::string serialized;
  task.SerializeToString(&serialized);

  auto wire = SerializeTlvFrame(TlvFrame::kTaskSubmission,
                                std::as_bytes(std::span(serialized.data(), serialized.size())));

  // Parse it back with TlvParser
  std::vector<TlvFrame> received_frames;
  TlvParser parser([&received_frames](TlvFrame frame) { received_frames.push_back(frame); });

  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), wire.data(), wire.size());
  parser.OnData(wire.size());

  ASSERT_EQ(received_frames.size(), 1U);
  EXPECT_EQ(received_frames[0].type_id, TlvFrame::kTaskSubmission);
  EXPECT_EQ(received_frames[0].value.size(), serialized.size());

  carrot::task::Task parsed;
  ASSERT_TRUE(parsed.ParseFromArray(received_frames[0].value.data(),
                                    static_cast<int>(received_frames[0].value.size())));
  EXPECT_EQ(parsed.id(), "42");
  EXPECT_EQ(parsed.type(), "echo");
  EXPECT_EQ(parsed.body(), "hello");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace carrot::io
