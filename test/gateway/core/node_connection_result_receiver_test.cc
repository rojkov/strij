#include <sys/socket.h>
#include <unistd.h>

#include <bit>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "common/core/io/connection.hh"
#include "common/core/io/outbound_mailbox.hh"
#include "common/core/io/protocol_parser.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/io/tlv_parser.hh"
#include "common/task/task.pb.h"
#include "gateway/core/node_connection_result_receiver.hh"
#include "gtest/gtest.h"

namespace strij::gateway {
namespace {

auto MakeReceiver(const std::string& task_id, std::vector<std::vector<std::byte>>& captured)
    -> std::unique_ptr<NodeConnectionResultReceiver> {
  auto mailbox = std::make_shared<io::OutboundMailbox>(
      [&captured](std::vector<std::byte> frame) { captured.push_back(std::move(frame)); });
  return std::make_unique<NodeConnectionResultReceiver>(task_id, mailbox);
}

auto ParseFrames(const std::vector<std::byte>& wire) -> std::vector<io::TlvFrame> {
  std::vector<io::TlvFrame> frames;
  io::TlvParser parser([&frames](io::TlvFrame frm) { frames.push_back(frm); });
  auto buf = parser.GetReadBuffer();
  std::memcpy(buf.data(), wire.data(), wire.size());
  parser.OnData(wire.size());
  return frames;
}

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(NodeConnectionResultReceiverTest, DeliverWritesFinalResultFrame) {
  std::vector<std::vector<std::byte>> captured;
  auto receiver = MakeReceiver("t1", captured);

  std::string body = "hello";
  receiver->Deliver(std::as_bytes(std::span(body)), /*is_final=*/true);

  ASSERT_EQ(captured.size(), 1U);
  auto frames = ParseFrames(captured[0]);
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].type_id, io::TlvFrame::kResult);

  task::TaskResult result;
  ASSERT_TRUE(result.ParseFromArray(std::bit_cast<const char*>(frames[0].value.data()),
                                    static_cast<int>(frames[0].value.size())));
  EXPECT_EQ(result.id(), "t1");
  EXPECT_EQ(result.body(), "hello");
  EXPECT_TRUE(result.is_final());
}

TEST(NodeConnectionResultReceiverTest, DeliverWritesNonFinalResultFrameExplicitly) {
  std::vector<std::vector<std::byte>> captured;
  auto receiver = MakeReceiver("t1", captured);

  std::string body = "chunk";
  receiver->Deliver(std::as_bytes(std::span(body)), /*is_final=*/false);

  ASSERT_EQ(captured.size(), 1U);
  auto frames = ParseFrames(captured[0]);
  ASSERT_EQ(frames.size(), 1U);
  task::TaskResult result;
  ASSERT_TRUE(result.ParseFromArray(std::bit_cast<const char*>(frames[0].value.data()),
                                    static_cast<int>(frames[0].value.size())));
  // Proto3: the optional is_final must be explicitly false for non-final chunks.
  EXPECT_TRUE(result.has_is_final());
  EXPECT_FALSE(result.is_final());
}

TEST(NodeConnectionResultReceiverTest, DeliverErrorWritesRejectedFrame) {
  std::vector<std::vector<std::byte>> captured;
  auto receiver = MakeReceiver("t1", captured);

  receiver->DeliverError("capacity exhausted");

  ASSERT_EQ(captured.size(), 1U);
  auto frames = ParseFrames(captured[0]);
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].type_id, io::TlvFrame::kTaskRejected);

  task::TaskRejected rejected;
  ASSERT_TRUE(rejected.ParseFromArray(std::bit_cast<const char*>(frames[0].value.data()),
                                      static_cast<int>(frames[0].value.size())));
  EXPECT_EQ(rejected.id(), "t1");
  EXPECT_EQ(rejected.reason(), "capacity exhausted");
}

TEST(NodeConnectionResultReceiverTest, DeliveryOnClosedMailboxIsNoop) {
  // A real Connection owns the mailbox; its teardown closes the mailbox, after
  // which Enqueue() is a no-op (the receiver may outlive its connection).
  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  io::Connection conn(fds[0], dispatcher, &owner, [](io::Connection&) -> io::ProtocolParserPtr {
    return std::make_unique<io::TrivialParser>();
  });

  NodeConnectionResultReceiver receiver("t1", conn.Mailbox());
  conn.Close();

  // After Close(), Enqueue is a no-op — no assertions fire and no data flows.
  std::string body = "late";
  receiver.Deliver(std::as_bytes(std::span(body)), true);
  receiver.DeliverError("late error");

  close(fds[1]);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::gateway