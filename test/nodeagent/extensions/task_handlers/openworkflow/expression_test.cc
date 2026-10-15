#include "nodeagent/extensions/task_handlers/openworkflow/expression.hh"

#include <string>
#include <vector>

#include "common/core/utils/jv.hh"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/nodeagent/extensions/task_handlers/openworkflow/fakes.hh"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

using ::strij::openworkflow::Value;

// NOLINTBEGIN(modernize-use-trailing-return-type)
using ::testing::UnorderedElementsAre;

TEST(ExpressionRecognitionTest, EntirelyWrappedStringsAreExpressions) {
  EXPECT_TRUE(IsExpression("${ .x }"));
  EXPECT_TRUE(IsExpression("${.x}"));
  EXPECT_FALSE(IsExpression(".x"));
  EXPECT_FALSE(IsExpression("text ${ .x } text"));
}

TEST(ExpressionRecognitionTest, PartialWrappersAreDetected) {
  EXPECT_TRUE(HasPartialWrapper("jobs/${ .id }"));
  EXPECT_TRUE(HasPartialWrapper("${ .id } trailing"));
  EXPECT_FALSE(HasPartialWrapper(".id"));
  EXPECT_FALSE(HasPartialWrapper("${ .id }"));
}

TEST(ExpressionRecognitionTest, ExpressionProgramStripsWrapperAndTrims) {
  EXPECT_EQ(ExpressionProgram("${ .x }"), ".x");
  EXPECT_EQ(ExpressionProgram("${.x}"), ".x");
}

TEST(ExpressionScanTest, FindsNestedExpressionsAndSkipsNonStrings) {
  const Value tree{Value::Object{
      {"a", Value{"${ .x }"}},
      {"b", Value{Value::Array{Value{Value::Object{{"c", Value{"${ .y }"}}}}}}},
      {"number", Value{1}},
      {"boolean", Value{true}},
      {"null", Value{}},
      {"literal", Value{".z"}},
  }};
  std::vector<std::string> programs;
  ASSERT_TRUE(CollectExpressions(tree, programs).ok());
  EXPECT_THAT(programs, UnorderedElementsAre(".x", ".y"));
}

TEST(ExpressionScanTest, RejectsPartialWrapperAtDepth) {
  const Value tree{Value::Object{
      {"a", Value{Value::Array{Value{Value::Object{{"b", Value{"jobs/${ .id }"}}}}}}},
  }};
  std::vector<std::string> programs;
  EXPECT_FALSE(CollectExpressions(tree, programs).ok());
}

TEST(ExpressionEngineTest, CompilesValidAndRejectsMalformed) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  EXPECT_TRUE(engine.Compile(".field").ok());
  EXPECT_FALSE(engine.Compile(".field(").ok());
}

TEST(ExpressionEngineTest, ValidatesEveryNestedExpression) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  const Value good{Value::Object{{"a", Value{"${ .x }"}}}};
  EXPECT_TRUE(engine.Validate(good).ok());

  const Value malformed{Value::Object{
      {"a", Value{Value::Object{{"b", Value{"${ .x( }"}}}}}}};
  EXPECT_FALSE(engine.Validate(malformed).ok());
}

TEST(ExpressionEngineTest, EvaluatesMixedObjectBesideLiteral) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  const Value tree{Value::Object{
      {"literal", Value{"unchanged"}},
      {"evaluated", Value{"${ .name }"}},
      {"nested", Value{Value::Array{Value{"${ .name }"}, Value{"plain"}}}},
  }};
  auto subject = utils::Jv::Parse(R"({"name":"widget"})");
  ASSERT_TRUE(subject.ok());

  auto result = engine.Evaluate(tree, *subject);
  ASSERT_TRUE(result.ok()) << result.status().message();
  const auto& object = result->AsObject();
  EXPECT_EQ(object.at("literal").AsString(), "unchanged");
  EXPECT_EQ(object.at("evaluated").AsString(), "widget");
  EXPECT_EQ(object.at("nested").AsArray()[0].AsString(), "widget");
  EXPECT_EQ(object.at("nested").AsArray()[1].AsString(), "plain");
}

TEST(ExpressionEngineTest, SubjectIsNotContext) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  auto subject = utils::Jv::Parse(R"({"name":"widget"})");
  ASSERT_TRUE(subject.ok());

  auto from_subject = engine.Run(".name", *subject);
  ASSERT_TRUE(from_subject.ok()) << from_subject.status().message();
  EXPECT_EQ(from_subject->Dump(), R"("widget")");

  auto from_context = engine.Run("$context.name", *subject);
  ASSERT_TRUE(from_context.ok()) << from_context.status().message();
  EXPECT_EQ(from_context->Dump(), "null");
}

TEST(RenderScalarTest, RendersStringUnquotedAndNumberCanonically) {
  auto text = RenderScalar(*utils::Jv::Parse(R"("/bin/task.sh")"));
  ASSERT_TRUE(text.ok());
  EXPECT_EQ(*text, "/bin/task.sh");

  auto number = RenderScalar(*utils::Jv::Parse("42"));
  ASSERT_TRUE(number.ok());
  EXPECT_EQ(*number, "42");
}

TEST(RenderScalarTest, RejectsNullAndNonScalars) {
  EXPECT_FALSE(RenderScalar(*utils::Jv::Parse("null")).ok());
  EXPECT_FALSE(RenderScalar(*utils::Jv::Parse("{}")).ok());
  EXPECT_FALSE(RenderScalar(*utils::Jv::Parse("[]")).ok());
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
