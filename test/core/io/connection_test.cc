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

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::io
