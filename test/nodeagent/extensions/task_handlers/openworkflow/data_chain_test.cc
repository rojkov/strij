#include "nodeagent/extensions/task_handlers/openworkflow/data_chain.hh"

#include <string>

#include "gtest/gtest.h"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(DataChainTest, JsonTextClassifiesAsJson) {
  DataChain chain = DataChain::FromBytes(R"({"a":1,"b":[true,null]})");
  EXPECT_TRUE(chain.IsJson());
  EXPECT_EQ(chain.Json().Dump(), R"({"a":1,"b":[true,null]})");
  EXPECT_EQ(chain.Bytes(), R"({"a":1,"b":[true,null]})");
}

TEST(DataChainTest, NonJsonTextStaysOpaque) {
  DataChain chain = DataChain::FromBytes("hello, world");
  EXPECT_FALSE(chain.IsJson());
  EXPECT_EQ(chain.Bytes(), "hello, world");
  EXPECT_EQ(chain.ToValue().AsString(), "hello, world");
}

TEST(DataChainTest, EmptyInputIsOpaque) {
  DataChain chain = DataChain::FromBytes("");
  EXPECT_FALSE(chain.IsJson());
  EXPECT_EQ(chain.Bytes(), "");
}

TEST(DataChainTest, FromValueIsJson) {
  DataChain chain = DataChain::FromValue(
      ::strij::openworkflow::Value{::strij::openworkflow::Value::Object{{"name", "widget"}}});
  EXPECT_TRUE(chain.IsJson());
  EXPECT_EQ(chain.Json().Dump(), R"({"name":"widget"})");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
