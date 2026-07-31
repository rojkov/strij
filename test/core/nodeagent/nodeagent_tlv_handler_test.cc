#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <memory>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "core/io/connection.hh"
#include "core/io/protocol_parser.hh"
#include "core/io/tlv_frame.hh"
#include "core/io/tlv_parser.hh"
#include "core/nodeagent/nodeagent_tlv_handler.hh"
#include "core/task/task.pb.h"
#include "gtest/gtest.h"

namespace carrot::nodeagent {
namespace {

class NodeagentTlvHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds_));
    dispatcher_ = std::make_shared<carrot::event::MockDispatcher>();
    // Suppress the PrepareRead issued by the Connection constructor.
    EXPECT_CALL(*dispatcher_,
                PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_,
                            ::testing::_))
        .WillOnce(::testing::Return());
    conn_ = std::make_unique<carrot::io::Connection>(
        fds_[0], dispatcher_, &owner_,
        [](carrot::io::Connection&) -> std::unique_ptr<carrot::io::ProtocolParser> {
          return std::make_unique<carrot::io::TrivialParser>();
        });
  }

  void TearDown() override {
    conn_.reset();
    close(fds_[0]);
    close(fds_[1]);
  }

  // Reads up to `size` bytes written by the handler into `buf`.
  // Returns the number of bytes read, or -1 if nothing was written.
  auto ReadWritten(std::span<std::byte> buf) -> ssize_t {
    return ::recv(fds_[1], buf.data(), buf.size(), MSG_DONTWAIT);
  }

  int fds_[2];
  std::shared_ptr<carrot::event::MockDispatcher> dispatcher_;
  carrot::event::DummyOwner owner_;
  std::unique_ptr<carrot::io::Connection> conn_;
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(NodeagentTlvHandlerTest, EchoesTaskAsTaskResult) {
  carrot::task::Task task;
  task.set_id("42");
  task.set_type("echo");
  task.set_body("hello");
  std::string serialized;
  task.SerializeToString(&serialized);
  auto wire = carrot::io::SerializeTlvFrame(carrot::io::TlvFrame::kTaskSubmission,
                                std::as_bytes(std::span(serialized.data(), serialized.size())));

  // When Connection::Write submits, perform the actual write to the socket.
  EXPECT_CALL(*dispatcher_,
              PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .WillOnce(::testing::Invoke(
          [this](carrot::event::Completable*, uint8_t, int, std::span<const std::byte> buf,
                 off_t) { ::write(fds_[0], buf.data(), buf.size()); }));

  NodeagentTlvHandler handler;
  handler.HandleFrame(
      {.type_id = carrot::io::TlvFrame::kTaskSubmission,
       .value = std::as_bytes(std::span(serialized))},
      *conn_);

  std::array<std::byte, 1024> buf{};
  ssize_t n = ReadWritten(buf);
  ASSERT_GT(n, 5);

  EXPECT_EQ(static_cast<uint8_t>(buf[0]), carrot::io::TlvFrame::kResult);
  uint32_t net_len{};
  std::memcpy(&net_len, buf.data() + 1, 4);
  uint32_t length = ntohl(net_len);
  EXPECT_EQ(length, static_cast<uint32_t>(n - 5));

  carrot::task::TaskResult result;
  ASSERT_TRUE(result.ParseFromArray(buf.data() + 5, static_cast<int>(n - 5)));
  EXPECT_EQ(result.id(), "42");
  EXPECT_EQ(result.body(), "hello");
}

TEST_F(NodeagentTlvHandlerTest, DropsMalformedTask) {
  auto garbage = std::vector<std::byte>{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE},
                                        std::byte{0xEF}};
  auto wire = carrot::io::SerializeTlvFrame(carrot::io::TlvFrame::kTaskSubmission, garbage);

  EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .Times(0);

  NodeagentTlvHandler handler;
  handler.HandleFrame(
      {.type_id = carrot::io::TlvFrame::kTaskSubmission, .value = garbage}, *conn_);

  std::array<std::byte, 16> buf{};
  EXPECT_EQ(ReadWritten(buf), -1);
  // errno should be EAGAIN/EWOULDBLOCK when nothing was written.
  EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace carrot::nodeagent
