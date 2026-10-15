#include "openworkflow/value_jv.hh"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "common/core/utils/jv.hh"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openworkflow/value.hh"

namespace strij::openworkflow {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(ValueJvTest, RoundTripsEachAlternative) {
  const Value null_value{};
  const Value bool_value{true};
  const Value int_value{std::int64_t{-42}};
  const Value double_value{3.5};
  const Value string_value{std::string{"hello"}};
  const Value array_value{Value::Array{Value{std::int64_t{1}}, Value{std::string{"two"}}}};
  const Value object_value{
      Value::Object{{"a", Value{std::int64_t{1}}}, {"b", Value{std::string{"two"}}}}};

  EXPECT_EQ(JvToValue(ValueToJv(null_value)), null_value);
  EXPECT_EQ(JvToValue(ValueToJv(bool_value)), bool_value);
  EXPECT_EQ(JvToValue(ValueToJv(int_value)), int_value);
  EXPECT_EQ(JvToValue(ValueToJv(double_value)), double_value);
  EXPECT_EQ(JvToValue(ValueToJv(string_value)), string_value);
  EXPECT_EQ(JvToValue(ValueToJv(array_value)), array_value);
  EXPECT_EQ(JvToValue(ValueToJv(object_value)), object_value);
}

TEST(ValueJvTest, RoundTripsMixedNestedStructure) {
  const Value nested{Value::Object{
      {"name", Value{std::string{"widget"}}},
      {"count", Value{std::int64_t{3}}},
      {"active", Value{true}},
      {"ratio", Value{0.25}},
      {"tags", Value{Value::Array{Value{std::string{"a"}}, Value{std::string{"b"}}}}},
      {"missing", Value{}},
      {"nested", Value{Value::Object{{"deep", Value{Value::Array{Value{std::int64_t{7}}}}}}}},
  }};

  EXPECT_EQ(JvToValue(ValueToJv(nested)), nested);
}

TEST(ValueJvTest, IntegerExtremesAreLossless) {
  const Value min_value{std::numeric_limits<std::int64_t>::min()};
  const Value max_value{std::numeric_limits<std::int64_t>::max()};

  const Value min_round_trip = JvToValue(ValueToJv(min_value));
  const Value max_round_trip = JvToValue(ValueToJv(max_value));

  ASSERT_TRUE(min_round_trip.IsInt());
  ASSERT_TRUE(max_round_trip.IsInt());
  EXPECT_EQ(min_round_trip.AsInt(), std::numeric_limits<std::int64_t>::min());
  EXPECT_EQ(max_round_trip.AsInt(), std::numeric_limits<std::int64_t>::max());
}

TEST(ValueJvTest, DoubleAndIntegerOneAreDistinct) {
  const Value integer_one{std::int64_t{1}};
  const Value double_one{1.0};

  const Value integer_round_trip = JvToValue(ValueToJv(integer_one));
  const Value double_round_trip = JvToValue(ValueToJv(double_one));

  EXPECT_TRUE(integer_round_trip.IsInt());
  EXPECT_TRUE(double_round_trip.IsDouble());
  EXPECT_NE(integer_round_trip, double_round_trip);
}

TEST(ValueJvTest, ParsedJsonPreservesIntegerAndDoubleKinds) {
  auto parsed_integer = utils::Jv::Parse("1");
  auto parsed_double = utils::Jv::Parse("1.0");
  ASSERT_TRUE(parsed_integer.ok());
  ASSERT_TRUE(parsed_double.ok());

  EXPECT_TRUE(JvToValue(*parsed_integer).IsInt());
  EXPECT_TRUE(JvToValue(*parsed_double).IsDouble());
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::openworkflow
