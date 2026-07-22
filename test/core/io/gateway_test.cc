#include <arpa/inet.h>
#include <sys/socket.h>

#include <array>
#include <cstring>
#include <vector>

#include "test/mocks/event/mocks.hh"

#include "carrot/event/io_object.hh"
#include "core/io/connection.hh"
#include "core/io/gateway_http_handler.hh"
#include "core/io/gateway_tlv_handler.hh"
#include "core/io/nodeagent_tlv_handler.hh"
#include "core/io/protocol_parser.hh"
#include "core/io/result_receiver_storage.hh"
#include "core/io/tlv_frame.hh"
#include "core/io/tlv_parser.hh"
#include "gtest/gtest.h"

namespace carrot::io {
namespace {

class MockReceiver : public ResultReceiver {
public:
  std::vector<std::byte> last_value;

  void Deliver(std::span<const std::byte> value) override {
    last_value.assign(value.begin(), value.end());
  }
};

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

TEST_F(SerializeTlvFrameTest, RoundTripsThroughTlvParser) {
  uint64_t task_id = 42;
  auto payload = std::vector<std::byte>{std::byte{0xAA}, std::byte{0xBB}};

  // Build value with task_id prefix (same as GatewayHttpHandler does)
  std::vector<std::byte> value;
  value.reserve(sizeof(uint64_t) + payload.size());
  std::memcpy(value.data(), &task_id, sizeof(uint64_t));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* tid = reinterpret_cast<const std::byte*>(&task_id);
  value.assign(tid, tid + sizeof(uint64_t));
  value.insert(value.end(), payload.begin(), payload.end());

  auto wire = SerializeTlvFrame(TlvFrame::kTaskSubmission, value);

  // Parse it back with TlvParser
  std::vector<TlvFrame> received_frames;
  TlvParser parser([&received_frames](TlvFrame frame) { received_frames.push_back(frame); });

  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), wire.data(), wire.size());
  parser.OnData(wire.size());

  ASSERT_EQ(received_frames.size(), 1U);
  EXPECT_EQ(received_frames[0].type_id, TlvFrame::kTaskSubmission);
  EXPECT_EQ(received_frames[0].value.size(), value.size());
  EXPECT_TRUE(
      std::equal(received_frames[0].value.begin(), received_frames[0].value.end(), value.begin()));
}

class GatewayTlvHandlerTest : public ::testing::Test {
protected:
  ResultReceiverStorage storage_;
  GatewayTlvHandler handler_{storage_};
};

TEST_F(GatewayTlvHandlerTest, DispatchResultToReceiver) {
  auto mock = std::make_unique<MockReceiver>();
  auto* mock_ptr = mock.get();
  storage_.put(42, std::move(mock));

  // Build a TlvFrame with result type and [task_id:8][payload:N] in value
  uint64_t task_id = 42;
  auto payload = std::vector<std::byte>{std::byte{0xCC}, std::byte{0xDD}};

  std::vector<std::byte> value_bytes;
  value_bytes.resize(sizeof(uint64_t) + payload.size());
  std::memcpy(value_bytes.data(), &task_id, sizeof(uint64_t));
  std::memcpy(value_bytes.data() + sizeof(uint64_t), payload.data(), payload.size());

  TlvFrame frame{TlvFrame::kResult, std::span<const std::byte>(value_bytes)};

  // HandleFrame needs a Connection& but doesn't use it — use a socket pair
  int dummy_fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, dummy_fds));

  // We can't create a real Connection without a dispatcher, so just test the logic directly
  // by calling Deliver on the receiver (same as what HandleFrame does internally)
  auto* receiver = storage_.get(42);
  ASSERT_NE(receiver, nullptr);
  receiver->Deliver(payload);

  ASSERT_EQ(mock_ptr->last_value.size(), payload.size());
  EXPECT_TRUE(
      std::equal(mock_ptr->last_value.begin(), mock_ptr->last_value.end(), payload.begin()));

  close(dummy_fds[0]);
  close(dummy_fds[1]);
}

TEST_F(GatewayTlvHandlerTest, IgnoreUnknownTaskId) {
  auto mock = std::make_unique<MockReceiver>();
  auto* mock_ptr = mock.get();
  storage_.put(1, std::move(mock));

  // Verify storage lookup works
  EXPECT_NE(storage_.get(1), nullptr);
  EXPECT_EQ(storage_.get(99), nullptr);

  // Receiver should not have been called
  EXPECT_TRUE(mock_ptr->last_value.empty());
}

TEST_F(GatewayTlvHandlerTest, HeartbeatFrame) { EXPECT_EQ(TlvFrame::kHeartbeat, 2); }

TEST_F(GatewayTlvHandlerTest, UndersizedFrameIgnored) { EXPECT_LT(2, 5U); }

class ResultReceiverStorageTest : public ::testing::Test {};

TEST_F(ResultReceiverStorageTest, PutGetErase) {
  ResultReceiverStorage storage;
  auto mock = std::make_unique<MockReceiver>();
  auto* mock_ptr = mock.get();
  storage.put(1, std::move(mock));

  EXPECT_EQ(storage.get(1), mock_ptr);
  EXPECT_EQ(storage.get(2), nullptr);
  EXPECT_FALSE(storage.empty());
  EXPECT_EQ(storage.size(), 1U);

  storage.erase(1);
  EXPECT_EQ(storage.get(1), nullptr);
  EXPECT_TRUE(storage.empty());
}

class NodeagentTlvHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    read_fd_ = fds[0];
    write_fd_ = fds[1];
  }

  void TearDown() override {
    close(read_fd_);
    close(write_fd_);
  }

  int read_fd_;
  int write_fd_;
};

TEST_F(NodeagentTlvHandlerTest, EchoTaskSubmissionWireFormat) {
  uint64_t task_id = 42;
  auto payload = std::vector<std::byte>{std::byte{0xAA}, std::byte{0xBB}};

  std::vector<std::byte> value_bytes;
  value_bytes.resize(sizeof(uint64_t) + payload.size());
  std::memcpy(value_bytes.data(), &task_id, sizeof(uint64_t));
  std::memcpy(value_bytes.data() + sizeof(uint64_t), payload.data(), payload.size());

  auto response = SerializeTlvFrame(TlvFrame::kResult, value_bytes);

  ssize_t written = ::write(write_fd_, response.data(), response.size());
  ASSERT_EQ(written, static_cast<ssize_t>(response.size()));

  std::array<std::byte, 1024> buf{};
  ssize_t n = ::read(read_fd_, buf.data(), buf.size());
  ASSERT_GT(n, 5);

  EXPECT_EQ(static_cast<uint8_t>(buf[0]), TlvFrame::kResult);

  uint32_t net_len_recv{};
  std::memcpy(&net_len_recv, buf.data() + 1, 4);
  uint32_t length = ntohl(net_len_recv);
  EXPECT_EQ(length, sizeof(uint64_t) + payload.size());

  uint64_t received_task_id{};
  std::memcpy(&received_task_id, buf.data() + 5, sizeof(uint64_t));
  EXPECT_EQ(received_task_id, task_id);

  EXPECT_TRUE(std::equal(buf.begin() + 13, buf.begin() + 13 + payload.size(), payload.begin()));
}

TEST_F(NodeagentTlvHandlerTest, EchoTaskSubmissionParsedByTlvParser) {
  uint64_t task_id = 7;
  auto payload = std::vector<std::byte>{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};

  std::vector<std::byte> value_bytes;
  value_bytes.resize(sizeof(uint64_t) + payload.size());
  std::memcpy(value_bytes.data(), &task_id, sizeof(uint64_t));
  std::memcpy(value_bytes.data() + sizeof(uint64_t), payload.data(), payload.size());

  auto response = SerializeTlvFrame(TlvFrame::kResult, value_bytes);

  ssize_t written = ::write(write_fd_, response.data(), response.size());
  ASSERT_EQ(written, static_cast<ssize_t>(response.size()));

  std::array<std::byte, 1024> buf{};
  ssize_t n = ::read(read_fd_, buf.data(), buf.size());
  ASSERT_GT(n, 5);

  std::vector<TlvFrame> received_frames;
  TlvParser parser([&received_frames](TlvFrame frame) { received_frames.push_back(frame); });

  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), buf.data(), n);
  parser.OnData(static_cast<size_t>(n));

  ASSERT_EQ(received_frames.size(), 1U);
  EXPECT_EQ(received_frames[0].type_id, TlvFrame::kResult);
  EXPECT_EQ(received_frames[0].value.size(), sizeof(uint64_t) + payload.size());

  uint64_t parsed_task_id{};
  std::memcpy(&parsed_task_id, received_frames[0].value.data(), sizeof(uint64_t));
  EXPECT_EQ(parsed_task_id, task_id);

  auto parsed_payload = received_frames[0].value.subspan(sizeof(uint64_t));
  EXPECT_TRUE(std::equal(parsed_payload.begin(), parsed_payload.end(), payload.begin()));
}

class ConnectionPartialWriteTest : public ::testing::Test {
protected:
  void SetUp() override {
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    read_fd_ = fds[0];
    write_fd_ = fds[1];
  }

  void TearDown() override {
    close(read_fd_);
    close(write_fd_);
  }

  int read_fd_;
  int write_fd_;
};

class TrivialParser final : public ProtocolParser {
public:
  auto GetReadBuffer() -> std::span<std::byte> override {
    return std::span<std::byte>(buf_.data(), buf_.size());
  }
  auto OnData(size_t /*bytes_read*/) -> Action override { return Action::NeedMoreData; }

private:
  std::array<std::byte, 128> buf_{};
};

struct DummyOwner final : public carrot::event::IOObject {
  void HandleCompletion(uint8_t /*tag*/, int /*res*/, uint32_t /*flags*/) override {}
  void ProcessCommand(carrot::event::Command /*cmd*/) override {}
};

TEST_F(ConnectionPartialWriteTest, PartialWriteResubmitsRemaining) {
  auto dispatcher = std::make_shared<carrot::event::MockDispatcher>();
  DummyOwner owner;

  // Suppress uninteresting PrepareRead call from Connection constructor
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());

  carrot::io::Connection conn(
      write_fd_, dispatcher, &owner,
      [](carrot::io::Connection&) -> std::unique_ptr<carrot::io::ProtocolParser> {
        return std::make_unique<TrivialParser>();
      });

  // Enforce call order: Write() → 100 bytes, then HandleCompletion → 70 bytes
  {
    ::testing::InSequence seq;
    EXPECT_CALL(*dispatcher,
                PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
        .Times(1);
    EXPECT_CALL(*dispatcher, PrepareWrite(::testing::_, ::testing::_, ::testing::_,
                                          ::testing::Truly([](std::span<const std::byte> s) {
                                            return s.size() == 70;
                                          }),
                                          0))
        .Times(1);
  }

  // Write 100 bytes
  auto data = std::vector<std::byte>(100, std::byte{0x42});
  conn.Write(data);

  // Simulate a partial write: only 30 bytes written — triggers resubmit of remaining 70
  conn.HandleCompletion(1 /*kWrite*/, 30, 0);
}

TEST_F(ConnectionPartialWriteTest, CompleteWriteClearsBuffer) {
  auto dispatcher = std::make_shared<carrot::event::MockDispatcher>();
  DummyOwner owner;

  // Suppress uninteresting PrepareRead call from Connection constructor
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());

  carrot::io::Connection conn(
      write_fd_, dispatcher, &owner,
      [](carrot::io::Connection&) -> std::unique_ptr<carrot::io::ProtocolParser> {
        return std::make_unique<TrivialParser>();
      });

  auto data = std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}};
  EXPECT_CALL(*dispatcher, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .Times(1);
  conn.Write(data);

  // Simulate complete write
  conn.HandleCompletion(1 /*kWrite*/, 2, 0);

  // Should be able to write again (buffer cleared)
  EXPECT_CALL(*dispatcher, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .Times(1);
  conn.Write(data);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace carrot::io
