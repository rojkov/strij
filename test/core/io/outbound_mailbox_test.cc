#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "core/io/connection.hh"
#include "core/io/outbound_mailbox.hh"
#include "core/io/protocol_parser.hh"
#include "gtest/gtest.h"

namespace strij::io {
namespace {

class OutboundMailboxTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds_));
    dispatcher_ = std::make_shared<strij::event::MockDispatcher>();
    // Suppress the PrepareRead issued by the Connection constructor.
    EXPECT_CALL(*dispatcher_,
                PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return());
  }

  void TearDown() override {
    close(fds_[0]);
    close(fds_[1]);
  }

  auto MakeConnection() -> std::unique_ptr<strij::io::Connection> {
    return std::make_unique<strij::io::Connection>(
        fds_[0], dispatcher_, &owner_,
        [](strij::io::Connection&) -> std::unique_ptr<strij::io::ProtocolParser> {
          return std::make_unique<strij::io::TrivialParser>();
        });
  }

  int fds_[2];
  std::shared_ptr<strij::event::MockDispatcher> dispatcher_;
  strij::event::DummyOwner owner_;
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(OutboundMailboxTest, EnqueueForwardsToConnectionWrite) {
  auto conn = MakeConnection();
  auto mailbox = conn->Mailbox();

  auto frame = std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
  EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_,
                                         ::testing::Truly([](std::span<const std::byte> s) {
                                           return s.size() == 3 && s[0] == std::byte{0x01} &&
                                                  s[2] == std::byte{0x03};
                                         }),
                                         0))
      .Times(1);
  mailbox->Enqueue(frame);
}

TEST_F(OutboundMailboxTest, EnqueueAfterCloseIsNoOp) {
  auto conn = MakeConnection();
  auto mailbox = conn->Mailbox();
  conn.reset();

  EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .Times(0);
  mailbox->Enqueue(std::vector<std::byte>{std::byte{0x42}});
}

TEST_F(OutboundMailboxTest, CloseFiresRegisteredCallbacksOnce) {
  auto conn = MakeConnection();
  auto mailbox = conn->Mailbox();

  int a = 0;
  int b = 0;
  mailbox->RegisterOnClose([&a] { ++a; });
  mailbox->RegisterOnClose([&b] { ++b; });

  conn.reset();

  EXPECT_EQ(a, 1);
  EXPECT_EQ(b, 1);
}

TEST_F(OutboundMailboxTest, UnregisterPreventsFiring) {
  auto conn = MakeConnection();
  auto mailbox = conn->Mailbox();

  int fired = 0;
  auto token = mailbox->RegisterOnClose([&fired] { ++fired; });
  mailbox->UnregisterOnClose(token);

  conn.reset();

  EXPECT_EQ(fired, 0);
}

TEST_F(OutboundMailboxTest, RegisterOnClosedMailboxFiresImmediately) {
  auto conn = MakeConnection();
  auto mailbox = conn->Mailbox();
  conn.reset();

  int fired = 0;
  mailbox->RegisterOnClose([&fired] { ++fired; });

  EXPECT_EQ(fired, 1);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::io
