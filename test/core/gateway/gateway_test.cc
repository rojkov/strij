#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "core/gateway/gateway_http_handler.hh"
#include "core/gateway/gateway_tlv_handler.hh"
#include "core/gateway/result_receiver_storage.hh"
#include "core/io/connection.hh"
#include "core/io/protocol_parser.hh"
#include "core/io/tlv_frame.hh"
#include "core/io/tlv_parser.hh"
#include "core/task/task.pb.h"
#include "gtest/gtest.h"

namespace strij::gateway {
namespace {

class MockReceiver : public ResultReceiver {
public:
  explicit MockReceiver(std::vector<std::byte>* out) : out_{out} {}

  void Deliver(std::span<const std::byte> value) override {
    out_->assign(value.begin(), value.end());
  }

private:
  std::vector<std::byte>* out_;
};

class ParseTaskTypeTest : public ::testing::Test {};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(ParseTaskTypeTest, ParsesTypeFromTaskPath) {
  auto type = ParseTaskType("/tasks/echo");
  ASSERT_TRUE(type.has_value());
  EXPECT_EQ(*type, "echo");
}

TEST_F(ParseTaskTypeTest, StripsQueryString) {
  auto type = ParseTaskType("/tasks/echo?param=1");
  ASSERT_TRUE(type.has_value());
  EXPECT_EQ(*type, "echo");
}

TEST_F(ParseTaskTypeTest, RejectsPathWithoutTaskPrefix) {
  auto type = ParseTaskType("/not-a-task");
  EXPECT_FALSE(type.has_value());
}

TEST_F(ParseTaskTypeTest, EmptyTypeForBareTasksPrefix) {
  auto type = ParseTaskType("/tasks/");
  ASSERT_TRUE(type.has_value());
  EXPECT_TRUE(type->empty());
}

TEST_F(ParseTaskTypeTest, EmptyTypeForTasksPrefixWithQuery) {
  auto type = ParseTaskType("/tasks/?param=1");
  ASSERT_TRUE(type.has_value());
  EXPECT_TRUE(type->empty());
}

class GatewayTlvHandlerTest : public ::testing::Test {
protected:
  ResultReceiverStorage storage_;
  GatewayTlvHandler handler_{storage_};
};

TEST_F(GatewayTlvHandlerTest, DispatchResultToReceiver) {
  std::vector<std::byte> delivered;
  storage_.put("42", std::make_unique<MockReceiver>(&delivered));

  strij::task::TaskResult result;
  result.set_id("42");
  result.set_body("CCDD");
  std::string serialized;
  result.SerializeToString(&serialized);
  auto wire = strij::io::SerializeTlvFrame(
      strij::io::TlvFrame::kResult,
      std::as_bytes(std::span(serialized.data(), serialized.size())));

  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  strij::io::Connection conn(fds[0], dispatcher, &owner,
                              [](strij::io::Connection&) -> std::unique_ptr<strij::io::ProtocolParser> {
                                return std::make_unique<strij::io::TrivialParser>();
                              });

  // Reconstruct the frame from wire bytes and deliver through the handler
  std::vector<strij::io::TlvFrame> received_frames;
  strij::io::TlvParser parser(
      [&received_frames](strij::io::TlvFrame frame) { received_frames.push_back(frame); });
  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), wire.data(), wire.size());
  parser.OnData(wire.size());
  ASSERT_EQ(received_frames.size(), 1U);

  handler_.HandleFrame(received_frames[0], conn);

  auto expected = std::vector<std::byte>{std::byte{'C'}, std::byte{'C'}, std::byte{'D'},
                                         std::byte{'D'}};
  ASSERT_EQ(delivered.size(), expected.size());
  EXPECT_TRUE(std::equal(delivered.begin(), delivered.end(), expected.begin()));
  EXPECT_EQ(storage_.get("42"), nullptr);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(GatewayTlvHandlerTest, UnknownTaskIdDropsResult) {
  std::vector<std::byte> delivered;
  storage_.put("1", std::make_unique<MockReceiver>(&delivered));

  strij::task::TaskResult result;
  result.set_id("99");
  result.set_body("data");
  std::string serialized;
  result.SerializeToString(&serialized);
  auto wire = strij::io::SerializeTlvFrame(
      strij::io::TlvFrame::kResult,
      std::as_bytes(std::span(serialized.data(), serialized.size())));

  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  strij::io::Connection conn(fds[0], dispatcher, &owner,
                              [](strij::io::Connection&) -> std::unique_ptr<strij::io::ProtocolParser> {
                                return std::make_unique<strij::io::TrivialParser>();
                              });

  std::vector<strij::io::TlvFrame> received_frames;
  strij::io::TlvParser parser(
      [&received_frames](strij::io::TlvFrame frame) { received_frames.push_back(frame); });
  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), wire.data(), wire.size());
  parser.OnData(wire.size());
  ASSERT_EQ(received_frames.size(), 1U);

  handler_.HandleFrame(received_frames[0], conn);

  EXPECT_TRUE(delivered.empty());
  EXPECT_NE(storage_.get("1"), nullptr);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(GatewayTlvHandlerTest, MalformedResultFrameIsDropped) {
  std::vector<std::byte> delivered;
  storage_.put("7", std::make_unique<MockReceiver>(&delivered));

  auto garbage = std::vector<std::byte>{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE},
                                        std::byte{0xEF}};
  auto wire = strij::io::SerializeTlvFrame(strij::io::TlvFrame::kResult, garbage);

  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  strij::io::Connection conn(fds[0], dispatcher, &owner,
                              [](strij::io::Connection&) -> std::unique_ptr<strij::io::ProtocolParser> {
                                return std::make_unique<strij::io::TrivialParser>();
                              });

  std::vector<strij::io::TlvFrame> received_frames;
  strij::io::TlvParser parser(
      [&received_frames](strij::io::TlvFrame frame) { received_frames.push_back(frame); });
  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), wire.data(), wire.size());
  parser.OnData(wire.size());
  ASSERT_EQ(received_frames.size(), 1U);

  handler_.HandleFrame(received_frames[0], conn);

  EXPECT_TRUE(delivered.empty());
  EXPECT_NE(storage_.get("7"), nullptr);

  close(fds[0]);
  close(fds[1]);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::gateway
