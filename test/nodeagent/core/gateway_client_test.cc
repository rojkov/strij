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
#include "common/core/io/tlv_frame.hh"
#include "common/core/io/tlv_parser.hh"
#include "common/task/task.pb.h"
#include "nodeagent/core/gateway_client.hh"
#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace strij::nodeagent {
namespace {

auto MakeTask(const std::string& id, const std::string& type) -> task::Task {
  task::Task task;
  task.set_id(id);
  task.set_type(type);
  return task;
}

// An OutboundMailbox whose enqueued bytes are captured for inspection.
class CapturingMailbox {
public:
  std::shared_ptr<io::OutboundMailbox> mailbox;
  std::vector<std::vector<std::byte>> frames;

  CapturingMailbox()
      : mailbox{std::make_shared<io::OutboundMailbox>(
            [this](std::vector<std::byte> frame) { frames.push_back(std::move(frame)); })} {}
};

// A Connection over a real socketpair whose mailbox is registered with the
// client, mirroring the nodeagent accept path.
class TestConnection {
public:
  event::DummyOwner owner;
  std::shared_ptr<event::MockDispatcher> dispatcher{std::make_shared<event::MockDispatcher>()};
  io::ConnectionPtr conn;

  // Returns nullptr when the socketpair cannot be created.
  static auto Create() -> std::unique_ptr<TestConnection> {
    std::array<int, 2> fds{};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) != 0) {
      return nullptr;
    }
    auto tc = std::unique_ptr<TestConnection>(new TestConnection());
    tc->fds_ = fds;
    EXPECT_CALL(*tc->dispatcher,
                PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Return());
    tc->conn = std::make_unique<io::Connection>(
        fds[0], tc->dispatcher, &tc->owner, [](io::Connection&) -> io::ProtocolParserPtr {
          return std::make_unique<io::TrivialParser>();
        });
    return tc;
  }

  explicit TestConnection() = default;

  ~TestConnection() {
    if (fds_[0] >= 0) {
      ::close(fds_[0]);
    }
    if (fds_[1] >= 0) {
      ::close(fds_[1]);
    }
  }

private:
  std::array<int, 2> fds_{-1, -1};
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(GatewayClientTest, ForwardWritesSubmissionOnLiveConnection) {
  GatewayClient client;
  CapturingMailbox m1;
  client.RegisterConnection(m1.mailbox);

  auto task = MakeTask("child-1", "echo");
  auto status = client.Forward(task);
  ASSERT_TRUE(status.ok());

  ASSERT_EQ(m1.frames.size(), 1U);
  std::vector<io::TlvFrame> parsed;
  io::TlvParser parser([&parsed](io::TlvFrame frame) { parsed.push_back(frame); });
  auto buf = parser.GetReadBuffer();
  std::memcpy(buf.data(), m1.frames[0].data(), m1.frames[0].size());
  parser.OnData(m1.frames[0].size());
  ASSERT_EQ(parsed.size(), 1U);
  EXPECT_EQ(parsed[0].type_id, io::TlvFrame::kTaskSubmission);
  task::Task roundtripped;
  ASSERT_TRUE(roundtripped.ParseFromArray(std::bit_cast<const char*>(parsed[0].value.data()),
                                          static_cast<int>(parsed[0].value.size())));
  EXPECT_EQ(roundtripped.id(), "child-1");
  EXPECT_EQ(roundtripped.type(), "echo");
}

TEST(GatewayClientTest, ForwardRoundRobinsAcrossLiveConnections) {
  GatewayClient client;
  CapturingMailbox m1;
  CapturingMailbox m2;
  CapturingMailbox m3;
  client.RegisterConnection(m1.mailbox);
  client.RegisterConnection(m2.mailbox);
  client.RegisterConnection(m3.mailbox);

  EXPECT_TRUE(client.Forward(MakeTask("1", "echo")).ok());
  EXPECT_TRUE(client.Forward(MakeTask("2", "echo")).ok());
  EXPECT_TRUE(client.Forward(MakeTask("3", "echo")).ok());

  EXPECT_EQ(m1.frames.size(), 1U);
  EXPECT_EQ(m2.frames.size(), 1U);
  EXPECT_EQ(m3.frames.size(), 1U);
  EXPECT_EQ(client.LiveCount(), 3U);
}

TEST(GatewayClientTest, ForwardWithoutLiveConnectionReturnsError) {
  GatewayClient client;
  auto status = client.Forward(MakeTask("child-1", "echo"));
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
}

TEST(GatewayClientTest, ClosedConnectionIsPruned) {
  GatewayClient client;
  {
    auto tc = TestConnection::Create();
    ASSERT_NE(tc, nullptr);
    client.RegisterConnection(tc->conn->Mailbox());
    EXPECT_EQ(client.LiveCount(), 1U);
    // Connection teardown fires the mailbox's close callback.
    tc->conn->Close();
  }
  EXPECT_EQ(client.LiveCount(), 0U);

  auto status = client.Forward(MakeTask("child-1", "echo"));
  EXPECT_FALSE(status.ok());
}

TEST(GatewayClientTest, ClosedConnectionSkipsButOtherConnectionsServe) {
  GatewayClient client;
  CapturingMailbox live;
  client.RegisterConnection(live.mailbox);
  {
    auto dead = TestConnection::Create();
    ASSERT_NE(dead, nullptr);
    client.RegisterConnection(dead->conn->Mailbox());
    dead->conn->Close();
  }

  EXPECT_TRUE(client.Forward(MakeTask("child-1", "echo")).ok());
  EXPECT_EQ(live.frames.size(), 1U);
  EXPECT_EQ(client.LiveCount(), 1U);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent