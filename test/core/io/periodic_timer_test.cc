#include <memory>

#include "test/mocks/event/mocks.hh"

#include "absl/time/time.h"
#include "core/io/periodic_timer.hh"
#include "gtest/gtest.h"

namespace strij::io {
namespace {

TEST(PeriodicTimerTest, StartArmsReadOnTimerFd) {
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  PeriodicTimer timer(dispatcher, [] {});

  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  timer.Start(absl::Seconds(1));
}

TEST(PeriodicTimerTest, EachTickFiresCallbackAndRearms) {
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  int ticks = 0;
  PeriodicTimer timer(dispatcher, [&ticks] { ++ticks; });

  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillRepeatedly(::testing::Return());
  timer.Start(absl::Seconds(1));

  timer.HandleCompletion(0, 8, 0);
  timer.HandleCompletion(0, 8, 0);

  EXPECT_EQ(ticks, 2);
}

TEST(PeriodicTimerTest, SubSecondIntervalsSupported) {
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  PeriodicTimer timer(dispatcher, [] {});

  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  timer.Start(absl::Milliseconds(250));
}

} // namespace
} // namespace strij::io
