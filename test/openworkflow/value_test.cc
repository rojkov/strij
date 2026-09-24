#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "openworkflow/value.hh"

namespace strij::openworkflow {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(ValueTest, DefaultsToNull) {
    const Value value;
    EXPECT_TRUE(value.IsNull());
}

TEST(ValueTest, HoldsEachScalarKind) {
    EXPECT_TRUE(Value(true).IsBool());
    EXPECT_EQ(Value(true).AsBool(), true);

    EXPECT_TRUE(Value(static_cast<std::int64_t>(42)).IsInt());
    EXPECT_EQ(Value(static_cast<std::int64_t>(42)).AsInt(), 42);

    EXPECT_TRUE(Value(3.5).IsDouble());
    EXPECT_DOUBLE_EQ(Value(3.5).AsDouble(), 3.5);

    EXPECT_TRUE(Value(std::string("hello")).IsString());
    EXPECT_EQ(Value(std::string("hello")).AsString(), "hello");
}

TEST(ValueTest, HoldsNestedArrayAndObject) {
    Value::Object inner;
    inner.emplace("name", Value(std::string("red")));
    inner.emplace("count", Value(static_cast<std::int64_t>(3)));

    Value::Array array;
    array.push_back(Value(std::move(inner)));
    array.push_back(Value(true));

    const Value value(std::move(array));
    ASSERT_TRUE(value.IsArray());
    ASSERT_EQ(value.AsArray().size(), 2U);
    ASSERT_TRUE(value.AsArray()[0].IsObject());
    EXPECT_EQ(value.AsArray()[0].AsObject().at("name").AsString(), "red");
    EXPECT_TRUE(value.AsArray()[1].AsBool());
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::openworkflow
