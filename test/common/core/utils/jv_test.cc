#include <string>

#include "common/core/utils/jv.hh"
#include "gtest/gtest.h"

namespace strij::utils {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(JvTest, ParsesAndDumpsJson) {
  auto jv = Jv::Parse(R"({"name":"world","n":42})");
  ASSERT_TRUE(jv.ok());
  EXPECT_EQ(jv->Dump(), R"({"name":"world","n":42})");
}

TEST(JvTest, ParseRejectsInvalidJson) {
  auto jv = Jv::Parse("{ not json ");
  EXPECT_FALSE(jv.ok());
}

TEST(JvTest, EmptyObjectDumpsAsObject) {
  auto jv = Jv::Parse("{}");
  ASSERT_TRUE(jv.ok());
  EXPECT_EQ(jv->Dump(), "{}");
}

TEST(JvTest, MoveOnlySemantics) {
  auto jv = Jv::Parse("[1,2]");
  ASSERT_TRUE(jv.ok());
  Jv moved(std::move(*jv));
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
  auto jv = Jv::Parse(R"({"a":[1,"x",null,true,{"b":2}]})");
  ASSERT_TRUE(jv.ok());
  EXPECT_EQ(jv->Dump(), R"({"a":[1,"x",null,true,{"b":2}]})");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::utils