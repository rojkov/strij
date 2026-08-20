#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "core/io/connection.hh"
#include "core/io/protocol_parser.hh"
#include "gtest/gtest.h"
#include "strij/event/command.hh"

namespace strij::io {
namespace {

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

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(ConnectionPartialWriteTest, PartialWriteResubmitsRemaining) {
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;

  // Suppress uninteresting PrepareRead call from Connection constructor
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());

  Connection conn(write_fd_, dispatcher, &owner,
                  [](Connection&) -> std::unique_ptr<ProtocolParser> {
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
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;

  // Suppress uninteresting PrepareRead call from Connection constructor
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());

  Connection conn(write_fd_, dispatcher, &owner,
                  [](Connection&) -> std::unique_ptr<ProtocolParser> {
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

TEST_F(ConnectionPartialWriteTest, QueuedWritesSubmitOnlyAfterPreviousCompletes) {
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;

  // Suppress uninteresting PrepareRead call from Connection constructor
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());

  Connection conn(write_fd_, dispatcher, &owner,
                  [](Connection&) -> std::unique_ptr<ProtocolParser> {
                    return std::make_unique<TrivialParser>();
                  });

  // Write A submits immediately; Write B (while A in-flight) only queues; the
  // next submission happens after A's completion and carries B's bytes.
  {
    ::testing::InSequence seq;
    EXPECT_CALL(*dispatcher, PrepareWrite(::testing::_, ::testing::_, ::testing::_,
                                          ::testing::Truly([](std::span<const std::byte> s) {
                                            return s.size() == 5 && s[0] == std::byte{0xAA} &&
                                                   s[4] == std::byte{0xAA};
                                          }),
                                          0))
        .Times(1);
    EXPECT_CALL(*dispatcher, PrepareWrite(::testing::_, ::testing::_, ::testing::_,
                                          ::testing::Truly([](std::span<const std::byte> s) {
                                            return s.size() == 3 && s[0] == std::byte{0xBB} &&
                                                   s[2] == std::byte{0xBB};
                                          }),
                                          0))
        .Times(1);
  }

  auto a = std::vector<std::byte>(5, std::byte{0xAA});
  auto b = std::vector<std::byte>(3, std::byte{0xBB});
  conn.Write(a);
  conn.Write(b);

  // A completes: B is submitted and drains.
  conn.HandleCompletion(1 /*kWrite*/, 5, 0);
  conn.HandleCompletion(1 /*kWrite*/, 3, 0);
}

TEST_F(ConnectionPartialWriteTest, PartialFrontWriteThenQueuedDrain) {
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;

  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());

  Connection conn(write_fd_, dispatcher, &owner,
                  [](Connection&) -> std::unique_ptr<ProtocolParser> {
                    return std::make_unique<TrivialParser>();
                  });

  {
    ::testing::InSequence seq;
    // Front buffer submitted initially (100 bytes).
    EXPECT_CALL(*dispatcher, PrepareWrite(::testing::_, ::testing::_, ::testing::_,
                                          ::testing::Truly([](std::span<const std::byte> s) {
                                            return s.size() == 100;
                                          }),
                                          0))
        .Times(1);
    // 40-byte partial write → resubmit 60 remaining bytes of the same buffer.
    EXPECT_CALL(*dispatcher, PrepareWrite(::testing::_, ::testing::_, ::testing::_,
                                          ::testing::Truly([](std::span<const std::byte> s) {
                                            return s.size() == 60;
                                          }),
                                          0))
        .Times(1);
    // Front completes → next queued buffer is submitted.
    EXPECT_CALL(*dispatcher, PrepareWrite(::testing::_, ::testing::_, ::testing::_,
                                          ::testing::Truly([](std::span<const std::byte> s) {
                                            return s.size() == 7;
                                          }),
                                          0))
        .Times(1);
  }

  auto a = std::vector<std::byte>(100, std::byte{0x42});
  auto b = std::vector<std::byte>(7, std::byte{0x24});
  conn.Write(a);
  conn.Write(b);

  conn.HandleCompletion(1 /*kWrite*/, 40, 0); // partial write of the front
  conn.HandleCompletion(1 /*kWrite*/, 60, 0); // front done, B drains
  conn.HandleCompletion(1 /*kWrite*/, 7, 0);
}

TEST_F(ConnectionPartialWriteTest, WriteErrorDropsQueuedWrites) {
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;

  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());

  Connection conn(write_fd_, dispatcher, &owner,
                  [](Connection&) -> std::unique_ptr<ProtocolParser> {
                    return std::make_unique<TrivialParser>();
                  });

  EXPECT_CALL(*dispatcher, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .Times(1);
  auto data = std::vector<std::byte>(4, std::byte{0x01});
  conn.Write(data);

  // A second write is queued while the first is in-flight.
  conn.Write(data);

  // Error completion drops the whole queue.
  conn.HandleCompletion(1 /*kWrite*/, -1, 0);

  // The queue is empty again: a fresh write submits immediately.
  EXPECT_CALL(*dispatcher, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .Times(1);
  conn.Write(data);
}

TEST_F(ConnectionPartialWriteTest, EndOfStreamClosesMailboxAndSubmitsCloseCommand) {
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;

  // Suppress uninteresting PrepareRead call from Connection constructor.
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());

  EXPECT_CALL(*dispatcher, SubmitCommand(::testing::AllOf(
                               ::testing::Field(&strij::event::Command::type_,
                                                 strij::event::Command::DEFERRED_DELETE),
                               ::testing::Field(&strij::event::Command::destination_, &owner))))
      .Times(1);

  Connection conn(write_fd_, dispatcher, &owner,
                  [](Connection&) -> std::unique_ptr<ProtocolParser> {
                    return std::make_unique<TrivialParser>();
                  });

  int fired = 0;
  conn.Mailbox()->RegisterOnClose([&fired] { ++fired; });

  // End-of-stream read completion closes the fd and the mailbox.
  conn.HandleCompletion(0 /*kRead*/, 0, 0);
  write_fd_ = -1; // onEndOfStream already closed it; avoid double-close.

  EXPECT_EQ(fired, 1);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::io
