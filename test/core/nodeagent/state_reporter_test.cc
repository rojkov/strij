#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "core/io/connection.hh"
#include "core/io/protocol_parser.hh"
#include "core/io/tlv_frame.hh"
#include "core/io/tlv_parser.hh"
#include "core/node/capabilities.pb.h"
#include "core/nodeagent/admission_controller.hh"
#include "core/nodeagent/state_reporter.hh"
#include "gtest/gtest.h"

namespace strij::nodeagent {
namespace {

auto MakeCapabilities() -> std::shared_ptr<const node::NodeCapabilities> {
  auto caps = std::make_shared<node::NodeCapabilities>();
  caps->set_node_id("node-x");
  caps->set_capability_version(1);
  auto* pool = caps->add_pools();
  pool->set_name("cpu");
  pool->set_total(16);
  return caps;
}

class StateReporterTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds_.data()));
    dispatcher_ = std::make_shared<event::MockDispatcher>();
    EXPECT_CALL(*dispatcher_,
                PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return());
    conn_ = std::make_unique<io::Connection>(fds_[0], dispatcher_, &owner_,
                                             [](io::Connection&) -> io::ProtocolParserPtr {
                                               return std::make_unique<io::TrivialParser>();
                                             });
  }

  void TearDown() override {
    conn_.reset();
    close(fds_[0]);
    close(fds_[1]);
  }

  static auto ParseBroadcast(std::span<const std::byte> bytes) -> node::NodeState {
    std::vector<io::TlvFrame> frames;
    io::TlvParser parser([&frames](io::TlvFrame frame) { frames.push_back(frame); });
    auto read_buf = parser.GetReadBuffer();
    std::memcpy(read_buf.data(), bytes.data(), bytes.size());
    parser.OnData(bytes.size());
    EXPECT_EQ(frames.size(), 1U);
    EXPECT_EQ(frames[0].type_id, io::TlvFrame::kNodeState);
    node::NodeState state;
    EXPECT_TRUE(state.ParseFromArray(std::bit_cast<const char*>(frames[0].value.data()),
                                     static_cast<int>(frames[0].value.size())));
    return state;
  }

  std::array<int, 2> fds_{};
  std::shared_ptr<event::MockDispatcher> dispatcher_;
  event::DummyOwner owner_;
  io::ConnectionPtr conn_;
};

TEST_F(StateReporterTest, BroadcastSendsNodeStateToRegisteredConnections) {
  auto caps = MakeCapabilities();
  auto admission = std::make_shared<AdmissionController>(*caps);
  ASSERT_TRUE(admission
                  ->Admit("echo",
                          [&] {
                            strij::node::ResourceRequirements requirements;
                            requirements.mutable_resources()->insert({"cpu", 2});
                            return requirements;
                          }())
                  .ok());

  auto reporter = std::make_shared<StateReporter>(admission, "node-x");
  reporter->AddConnection(conn_->Mailbox());

  std::span<const std::byte> written;
  EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .WillOnce(::testing::DoAll(::testing::SaveArg<3>(&written), ::testing::Return()));

  reporter->Broadcast();

  auto state = ParseBroadcast(written);
  EXPECT_EQ(state.node_id(), "node-x");
  EXPECT_EQ(state.seq(), 1U);
  EXPECT_GT(state.timestamp(), 0U);
  EXPECT_EQ(state.in_flight(), 1U);
  ASSERT_EQ(state.pools_size(), 1);
  EXPECT_EQ(state.pools(0).pool(), "cpu");
  EXPECT_EQ(state.pools(0).in_use(), 2U);
}

TEST_F(StateReporterTest, SequenceIncrementsAcrossBroadcasts) {
  auto admission = std::make_shared<AdmissionController>(*MakeCapabilities());
  auto reporter = std::make_shared<StateReporter>(admission, "node-x");
  reporter->AddConnection(conn_->Mailbox());

  std::span<const std::byte> first_written;
  EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .WillOnce(::testing::DoAll(::testing::SaveArg<3>(&first_written), ::testing::Return()));
  reporter->Broadcast();
  EXPECT_EQ(ParseBroadcast(first_written).seq(), 1U);

  // The mock dispatcher never completes writes on its own; simulate the first
  // write finishing so the connection drains its queue and issues the next.
  conn_->HandleCompletion(1 /*kWrite*/, static_cast<int>(first_written.size()), 0);

  std::span<const std::byte> second_written;
  EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .WillOnce(::testing::DoAll(::testing::SaveArg<3>(&second_written), ::testing::Return()));
  reporter->Broadcast();
  EXPECT_EQ(ParseBroadcast(second_written).seq(), 2U);
}

TEST_F(StateReporterTest, NoBroadcastWhenNoConnections) {
  auto admission = std::make_shared<AdmissionController>(*MakeCapabilities());
  auto reporter = std::make_shared<StateReporter>(admission, "node-x");

  EXPECT_CALL(*dispatcher_,
              PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(0);
  reporter->Broadcast();
}

TEST_F(StateReporterTest, ClosedConnectionIsUnregistered) {
  auto admission = std::make_shared<AdmissionController>(*MakeCapabilities());
  auto reporter = std::make_shared<StateReporter>(admission, "node-x");
  reporter->AddConnection(conn_->Mailbox());

  // Tear down the connection; the close callback must unregister the mailbox.
  conn_.reset();

  EXPECT_CALL(*dispatcher_,
              PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(0);
  reporter->Broadcast();
}

} // namespace
} // namespace strij::nodeagent
