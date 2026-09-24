#include <string>
#include <string_view>

#include <yaml-cpp/yaml.h>

#include "gtest/gtest.h"
#include "openworkflow/parser/errors.hh"
#include "openworkflow/parser/one_of.hh"

namespace strij::openworkflow::parser {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(ParseErrorTest, CarriesPositionAndMessage) {
    const YAML::Node node = YAML::Load("key: value");
    const ParseError error(node.Mark(), "boom");

    EXPECT_EQ(error.Message(), "boom");
    EXPECT_EQ(error.Position(), static_cast<std::size_t>(node.Mark().pos));
    EXPECT_EQ(error.mark.pos, node.Mark().pos);
}

TEST(ParseErrorTest, IsAYamlException) {
    const YAML::Node node = YAML::Load("key: value");
    const ParseError error(node.Mark(), "boom");

    const YAML::Exception& base = error;
    EXPECT_EQ(base.msg, "boom");
}

TEST(SelectOneTest, ReturnsTheSinglePresentKey) {
    const YAML::Node node = YAML::Load("call: http");
    EXPECT_EQ(SelectOne(node, {"call", "do", "wait"}, "task kind"), std::string_view("call"));
}

TEST(SelectOneTest, ThrowsWhenNoKeyPresent) {
    const YAML::Node node = YAML::Load("{other: 1}");
    try {
        static_cast<void>(SelectOne(node, {"call", "do"}, "task kind"));
        FAIL() << "expected ParseError";
    } catch (const ParseError& error) {
        EXPECT_FALSE(std::string_view(error.Message()).empty());
        EXPECT_NE(std::string_view(error.Message()).find("none"), std::string_view::npos);
    }
}

TEST(SelectOneTest, ThrowsWhenMultipleKeysPresent) {
    const YAML::Node node = YAML::Load("call: http\nwait: PT1S");
    try {
        static_cast<void>(SelectOne(node, {"call", "wait"}, "task kind"));
        FAIL() << "expected ParseError";
    } catch (const ParseError& error) {
        EXPECT_NE(std::string_view(error.Message()).find("mutually exclusive"),
                  std::string_view::npos);
        EXPECT_NE(std::string_view(error.Message()).find("call"), std::string_view::npos);
        EXPECT_NE(std::string_view(error.Message()).find("wait"), std::string_view::npos);
    }
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::openworkflow::parser
