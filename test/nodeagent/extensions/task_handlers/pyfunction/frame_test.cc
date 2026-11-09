#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "nodeagent/extensions/task_handlers/pyfunction/frame.hh"

namespace strij::nodeagent::task_handlers::pyfunction {
namespace {

auto Bytes(std::string_view text) -> std::vector<std::byte> {
  std::vector<std::byte> out;
  out.reserve(text.size());
  for (const char chr : text) {
    out.push_back(static_cast<std::byte>(chr));
  }
  return out;
}

auto AsString(const std::vector<std::byte>& bytes) -> std::string {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(FrameCodecTest, RoundTripsPayload) {
  const auto payload = Bytes("hello worker");
  auto frame = EncodeFrame(payload, 1024);
  ASSERT_TRUE(frame.ok());

  FrameDecoder decoder(1024);
  EXPECT_TRUE(decoder.Append(*frame).ok());
  auto decoded = decoder.Next();
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(AsString(*decoded), "hello worker");
  EXPECT_FALSE(decoder.Next().has_value());
}

TEST(FrameCodecTest, HandlesPartialPrefixAndPayload) {
  const auto payload = Bytes("abcdef");
  auto frame = EncodeFrame(payload, 1024);
  ASSERT_TRUE(frame.ok());

  FrameDecoder decoder(1024);
  // Only two prefix bytes.
  EXPECT_TRUE(decoder.Append(std::span<const std::byte>(frame->data(), 2)).ok());
  EXPECT_FALSE(decoder.Next().has_value());

  // Complete the prefix plus one payload byte.
  EXPECT_TRUE(decoder.Append(std::span<const std::byte>(frame->data() + 2, 3)).ok());
  EXPECT_FALSE(decoder.Next().has_value());

  // Remaining payload.
  EXPECT_TRUE(
      decoder.Append(std::span<const std::byte>(frame->data() + 5, frame->size() - 5)).ok());
  auto decoded = decoder.Next();
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(AsString(*decoded), "abcdef");
}

TEST(FrameCodecTest, RejectsOversizedDeclaredLengthBeforePayload) {
  const std::vector<std::byte> header = {std::byte{0x00}, std::byte{0x00}, std::byte{0x10},
                                         std::byte{0x00}}; // 4096
  FrameDecoder decoder(64);
  auto status = decoder.Append(header);
  EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
  EXPECT_FALSE(decoder.Next().has_value());
}

TEST(FrameCodecTest, RejectsOversizedPayloadOnEncode) {
  const auto payload = Bytes(std::string(65, 'x'));
  auto frame = EncodeFrame(payload, 64);
  EXPECT_EQ(frame.status().code(), absl::StatusCode::kResourceExhausted);
}

TEST(FrameCodecTest, ExtractsCoalescedFrames) {
  auto first = EncodeFrame(Bytes("one"), 1024);
  auto second = EncodeFrame(Bytes("two"), 1024);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());

  std::vector<std::byte> stream = *first;
  stream.insert(stream.end(), second->begin(), second->end());

  FrameDecoder decoder(1024);
  EXPECT_TRUE(decoder.Append(stream).ok());
  auto one = decoder.Next();
  auto two = decoder.Next();
  ASSERT_TRUE(one.has_value());
  ASSERT_TRUE(two.has_value());
  EXPECT_EQ(AsString(*one), "one");
  EXPECT_EQ(AsString(*two), "two");
  EXPECT_FALSE(decoder.Next().has_value());
}

TEST(FrameCodecTest, UsesBigEndianLengthPrefix) {
  auto frame = EncodeFrame(Bytes("abc"), 1024);
  ASSERT_TRUE(frame.ok());
  ASSERT_GE(frame->size(), 4U);
  EXPECT_EQ((*frame)[0], std::byte{0x00});
  EXPECT_EQ((*frame)[1], std::byte{0x00});
  EXPECT_EQ((*frame)[2], std::byte{0x00});
  EXPECT_EQ((*frame)[3], std::byte{0x03});
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::pyfunction
