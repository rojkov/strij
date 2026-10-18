#include "nodeagent/extensions/task_handlers/openworkflow/step_data_value.hh"

#include <string>

#include "gtest/gtest.h"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(StepDataValueTest, JsonTextClassifiesAsJson) {
  StepDataValue value = StepDataValue::FromBytes(R"({"a":1,"b":[true,null]})");
  EXPECT_TRUE(value.IsJson());
  EXPECT_EQ(value.Json().Dump(), R"({"a":1,"b":[true,null]})");
  EXPECT_EQ(value.Bytes(), R"({"a":1,"b":[true,null]})");
}

TEST(StepDataValueTest, NonJsonTextStaysOpaque) {
  StepDataValue value = StepDataValue::FromBytes("hello, world");
  EXPECT_FALSE(value.IsJson());
  EXPECT_EQ(value.Bytes(), "hello, world");
  EXPECT_EQ(value.ToValue().AsString(), "hello, world");
}

TEST(StepDataValueTest, EmptyInputIsOpaque) {
  StepDataValue value = StepDataValue::FromBytes("");
  EXPECT_FALSE(value.IsJson());
  EXPECT_EQ(value.Bytes(), "");
}

TEST(StepDataValueTest, FromValueIsJson) {
  StepDataValue value = StepDataValue::FromValue(
      ::strij::openworkflow::Value{::strij::openworkflow::Value::Object{{"name", "widget"}}});
  EXPECT_TRUE(value.IsJson());
  EXPECT_EQ(value.Json().Dump(), R"({"name":"widget"})");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
