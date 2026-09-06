#include <cstddef>
#include <optional>

#include "common/core/utils/bounded_queue.hh"
#include "gtest/gtest.h"

namespace strij::utils {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(BoundedQueueTest, PushAcceptsItemsUpToCapacity) {
  BoundedQueue<int> queue{3};
  EXPECT_TRUE(queue.Push(1));
  EXPECT_TRUE(queue.Push(2));
  EXPECT_TRUE(queue.Push(3));
  EXPECT_EQ(queue.Size(), 3U);
  EXPECT_TRUE(queue.Full());
}

TEST(BoundedQueueTest, FullQueueRejectsPush) {
  BoundedQueue<int> queue{2};
  ASSERT_TRUE(queue.Push(1));
  ASSERT_TRUE(queue.Push(2));

  EXPECT_FALSE(queue.Push(4));
  EXPECT_EQ(queue.Size(), 2U);
}

TEST(BoundedQueueTest, TryPopDrainsInFifoOrder) {
  BoundedQueue<int> queue{3};
  queue.Push(1);
  queue.Push(2);
  queue.Push(3);

  EXPECT_EQ(queue.TryPop(), std::optional<int>{1});
  EXPECT_EQ(queue.TryPop(), std::optional<int>{2});
  EXPECT_EQ(queue.TryPop(), std::optional<int>{3});
  EXPECT_TRUE(queue.Empty());
}

TEST(BoundedQueueTest, TryPopOnEmptyQueueIsDisengaged) {
  BoundedQueue<int> queue{4};
  EXPECT_FALSE(queue.TryPop().has_value());
}

TEST(BoundedQueueTest, ClearEmptiesQueue) {
  BoundedQueue<int> queue{2};
  queue.Push(1);
  queue.Push(2);

  queue.Clear();

  EXPECT_TRUE(queue.Empty());
  EXPECT_EQ(queue.Size(), 0U);
  EXPECT_FALSE(queue.Full());
}

TEST(BoundedQueueTest, AccessorsStayConsistent) {
  BoundedQueue<int> queue{5};
  EXPECT_TRUE(queue.Empty());
  EXPECT_FALSE(queue.Full());
  EXPECT_EQ(queue.MaxSize(), 5U);

  queue.Push(42);
  EXPECT_EQ(queue.Size(), 1U);
  ASSERT_NE(queue.Peek(), nullptr);
  EXPECT_EQ(*queue.Peek(), 42);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::utils