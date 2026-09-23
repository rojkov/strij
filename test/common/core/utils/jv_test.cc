#include <type_traits>
#include <utility>

#include "common/core/utils/jv.hh"
#include "gtest/gtest.h"

namespace strij::utils {
namespace {

static_assert(!std::is_copy_constructible_v<Jv>);
static_assert(!std::is_copy_assignable_v<Jv>);
static_assert(std::is_move_constructible_v<Jv>);
static_assert(std::is_move_assignable_v<Jv>);

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(JvTest, ParsesAndDumpsJson) {
  auto jvalue = Jv::Parse(R"({"name":"world","n":42})");
  ASSERT_TRUE(jvalue.ok());
  EXPECT_EQ(jvalue->Dump(), R"({"name":"world","n":42})");
}

TEST(JvTest, ParseRejectsInvalidJson) {
  auto jvalue = Jv::Parse("{ not json ");
  EXPECT_FALSE(jvalue.ok());
}

TEST(JvTest, EmptyObjectDumpsAsObject) {
  auto jvalue = Jv::Parse("{}");
  ASSERT_TRUE(jvalue.ok());
  EXPECT_EQ(jvalue->Dump(), "{}");
}

TEST(JvTest, MoveOnlySemantics) {
  auto jvalue = Jv::Parse("[1,2]");
  ASSERT_TRUE(jvalue.ok());
  Jv moved(std::move(*jvalue));
  EXPECT_EQ(moved.Dump(), "[1,2]");
}

TEST(JvTest, MoveAssignReleasesOldValue) {
  Jv first = Jv::Parse("1").value();
  Jv second = Jv::Parse("2").value();
  second = std::move(first);
  EXPECT_EQ(second.Dump(), "1");
}

TEST(JvTest, CopySharesData) {
  auto original = Jv::Parse(R"({"a":1})");
  ASSERT_TRUE(original.ok());
  auto copy = original->Copy();
  EXPECT_EQ(copy.Dump(), original->Dump());
  // Original still usable after copy is dropped.
  EXPECT_EQ(original->Dump(), R"({"a":1})");
}

TEST(JvTest, DefaultConstructedIsInvalid) {
  Jv empty;
  EXPECT_FALSE(empty.IsValid());
}

TEST(JvTest, AcquireTakesOwnership) {
  // Raw-handle plumbing for the evaluator category: an acquired handle is
  // still dumped correctly and frees cleanly on destruction.
  auto parsed = Jv::Parse("true");
  ASSERT_TRUE(parsed.ok());
  jv raw = JvRawCopy(*parsed);
  Jv owned = Acquire(raw);
  EXPECT_EQ(owned.Dump(), "true");
}

TEST(JvTest, RoundTripsNestedStructure) {
  auto jvalue = Jv::Parse(R"({"a":[1,"x",null,true,{"b":2}]})");
  ASSERT_TRUE(jvalue.ok());
  EXPECT_EQ(jvalue->Dump(), R"({"a":[1,"x",null,true,{"b":2}]})");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::utils