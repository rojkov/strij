#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <memory>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "common/core/io/connection.hh"
#include "common/core/io/tcp_listener.hh"
#include "gtest/gtest.h"
#include "liburing.h"

namespace strij::io {
namespace {

class TcpListenerTest : public ::testing::Test {
protected:
  void SetUp() override {
    dispatcher_ = std::make_shared<event::MockDispatcher>();
    EXPECT_CALL(*dispatcher_, PrepareAcceptMultishot(::testing::_, ::testing::_, ::testing::_))
        .Times(1);
  }

  std::shared_ptr<event::MockDispatcher> dispatcher_;
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(TcpListenerTest, AcceptDuringShutdownIsDroppedAndFdClosed) {
  bool factory_called = false;
  TcpListener listener{dispatcher_, 0, [&factory_called](Connection&) -> ProtocolParserPtr {
                         factory_called = true;
                         return std::make_unique<TrivialParser>();
                       }};

  std::array<int, 2> fds{-1, -1};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));

  EXPECT_CALL(*dispatcher_, IsShuttingDown()).WillOnce(::testing::Return(true));
  EXPECT_CALL(*dispatcher_,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(0);

  listener.HandleCompletion(0, fds[0], IORING_CQE_F_MORE);

  // The accepted fd is dropped instead of being retained by an inert
  // Connection whose read would never be submitted.
  errno = 0;
  EXPECT_EQ(fcntl(fds[0], F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
  EXPECT_FALSE(factory_called);

  close(fds[1]);
}

TEST_F(TcpListenerTest, CancelledAcceptDuringShutdownIsIgnored) {
  TcpListener listener{dispatcher_, 0, [](Connection&) -> ProtocolParserPtr {
                         return std::make_unique<TrivialParser>();
                       }};

  EXPECT_CALL(*dispatcher_, IsShuttingDown()).WillOnce(::testing::Return(true));
  EXPECT_CALL(*dispatcher_,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(0);

  listener.HandleCompletion(0, -ECANCELED, 0);
}

TEST_F(TcpListenerTest, AcceptedConnectionArmsRead) {
  TcpListener listener{dispatcher_, 0, [](Connection&) -> ProtocolParserPtr {
                         return std::make_unique<TrivialParser>();
                       }};

  std::array<int, 2> fds{-1, -1};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));

  EXPECT_CALL(*dispatcher_, IsShuttingDown()).WillOnce(::testing::Return(false));
  EXPECT_CALL(*dispatcher_, PrepareRead(::testing::_, 0, fds[0], ::testing::_, 0)).Times(1);

  listener.HandleCompletion(0, fds[0], IORING_CQE_F_MORE);

  // Connection does not close its fd in its destructor, so release the test's
  // end of the pair and the accepted fd explicitly.
  close(fds[1]);
  close(fds[0]);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::io
