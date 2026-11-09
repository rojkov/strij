#include <string>
#include <vector>

#include "test/nodeagent/extensions/task_handlers/openworkflow/fakes.hh"

#include "common/core/utils/jv.hh"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nodeagent/extensions/task_handlers/openworkflow/expression.hh"
#include "openworkflow/value_jv.hh"

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
  auto programs = CollectExpressions(tree);
  ASSERT_TRUE(programs.ok());
  EXPECT_THAT(*programs, UnorderedElementsAre(".x", ".y"));
}

TEST(ExpressionScanTest, RejectsPartialWrapperAtDepth) {
  const Value tree{Value::Object{
      {"a", Value{Value::Array{Value{Value::Object{{"b", Value{"jobs/${ .id }"}}}}}}},
  }};
  EXPECT_FALSE(CollectExpressions(tree).ok());
}

TEST(ExpressionEngineTest, CompilesValidAndRejectsMalformed) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  EXPECT_TRUE(engine.Compile(".field").ok());
  EXPECT_FALSE(engine.Compile(".field(").ok());
}

TEST(ExpressionEngineTest, ValidatesEveryNestedExpression) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  const Value good{Value::Object{{"a", Value{"${ .x }"}}}};
  EXPECT_TRUE(engine.Validate(good, kValue).ok());

  const Value malformed{Value::Object{{"a", Value{Value::Object{{"b", Value{"${ .x( }"}}}}}}};
  EXPECT_FALSE(engine.Validate(malformed, kValue).ok());
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

  auto result = engine.Evaluate(tree, kValue, *subject);
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

TEST(ExpressionEngineTest, RunAndEvaluateObserveTheSuppliedContext) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  auto subject = utils::Jv::Parse(R"({"name":"widget"})");
  ASSERT_TRUE(subject.ok());
  auto context = utils::Jv::Parse(R"({"token":"abc"})");
  ASSERT_TRUE(context.ok());

  const ExpressionArguments arguments{.context = &*context};
  auto run = engine.Run("$context.token", *subject, arguments);
  ASSERT_TRUE(run.ok()) << run.status().message();
  EXPECT_EQ(run->Dump(), R"("abc")");

  auto evaluated = engine.Evaluate(Value{Value::Object{{"token", Value{"${ $context.token }"}}}},
                                   kValue, *subject, arguments);
  ASSERT_TRUE(evaluated.ok()) << evaluated.status().message();
  EXPECT_EQ(evaluated->AsObject().at("token").AsString(), "abc");

  auto typed = engine.Evaluate(Value{"${ $context.token }"}, kIf, *subject, arguments);
  ASSERT_TRUE(typed.ok()) << typed.status().message();
  EXPECT_EQ(::strij::openworkflow::ValueToJv(*typed).Dump(), R"("abc")");
}

TEST(ExpressionEngineTest, InputAndOutputAreAbsentFromSlotsThatDoNotBindThem) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  auto context = utils::Jv::Parse(R"({"token":"abc"})");
  ASSERT_TRUE(context.ok());

  const ExpressionArguments arguments{.context = &*context};
  for (const ExpressionSlot& slot : {kIf, kInputFrom, kDocumentOutput}) {
    EXPECT_FALSE(engine.Validate(Value{"${ $input }"}, slot, arguments).ok());
    EXPECT_FALSE(engine.Validate(Value{"${ $output }"}, slot, arguments).ok());
  }

  EXPECT_TRUE(engine.Validate(Value{"${ $input }"}, kTaskExport).ok());
  EXPECT_TRUE(engine.Validate(Value{"${ $output }"}, kTaskExport).ok());
}

TEST(ExpressionEngineTest, ExpressionOnlySlotsRejectLiteralText) {
  ExpressionEngine engine(test::JqEvaluatorConfig());

  for (const ExpressionSlot& slot : {kIf, kSwitchWhen, kForIn, kForWhile}) {
    EXPECT_FALSE(engine.Validate(Value{".bare"}, slot).ok());
  }

  EXPECT_FALSE(engine.Validate(Value{"${ .x } tail"}, kIf).ok());
}

TEST(ExpressionEngineTest, ValueConstructionSlotsAcceptLiteralTextAndExpressions) {
  ExpressionEngine engine(test::JqEvaluatorConfig());

  for (const ExpressionSlot& slot : {kInputFrom, kTaskOutput, kTaskExport, kDocumentOutput}) {
    EXPECT_TRUE(engine.Validate(Value{".bare"}, slot).ok());
  }

  const Value nested{Value::Object{{"id", Value{".payload.id"}}}};
  EXPECT_TRUE(engine.Validate(nested, kInputFrom).ok());

  EXPECT_TRUE(engine.Validate(Value{"${ .enabled }"}, kIf).ok());
  EXPECT_TRUE(engine.Validate(Value{"${ .payload }"}, kInputFrom).ok());
  EXPECT_TRUE(engine.Validate(Value{"${ { token: .token } }"}, kTaskExport).ok());
  EXPECT_TRUE(
      engine.Validate(Value{Value::Object{{"id", Value{"${ .payload.id }"}}}}, kInputFrom).ok());

  // A partial wrapper or an unparsable program stays malformed in every slot.
  EXPECT_FALSE(engine.Validate(Value{"job-${ .id }"}, kInputFrom).ok());
  EXPECT_FALSE(engine.Validate(Value{"${ .x("}, kTaskExport).ok());
}

TEST(ExpressionEngineTest, SwitchWhenRequiresAWholeValueExpression) {
  ExpressionEngine engine(test::JqEvaluatorConfig());

  EXPECT_FALSE(engine.Validate(Value{".orderType == \"electronic\""}, kSwitchWhen).ok());
  EXPECT_FALSE(engine.Validate(Value{"${ .orderType( }"}, kSwitchWhen).ok());
  EXPECT_TRUE(engine.Validate(Value{"${ .orderType == \"electronic\" }"}, kSwitchWhen).ok());
}

TEST(ExpressionEngineTest, SwitchWhenBindsOnlyTheContext) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  const auto subject = *utils::Jv::Parse(R"({"enabled":true})");
  const auto context = *utils::Jv::Parse(R"({"token":"abc"})");
  const ExpressionArguments arguments{.context = &context};

  auto result = engine.Evaluate(Value{"${ $context.token }"}, kSwitchWhen, subject, arguments);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(::strij::openworkflow::ValueToJv(*result).Dump(), R"("abc")");

  EXPECT_FALSE(engine.Validate(Value{"${ $input }"}, kSwitchWhen).ok());
}

TEST(ExpressionEngineTest, ValueSlotEvaluationIsJsonFaithful) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  const auto subject = *utils::Jv::Parse(R"({"name":"world"})");

  auto result = engine.Evaluate(Value{"${ .name }"}, kInputFrom, subject);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(::strij::openworkflow::ValueToJv(*result).Dump(), R"("world")");
}

TEST(ExpressionEngineTest, TaskOutputSlotBindsInputAndOutput) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  const auto input = *utils::Jv::Parse(R"({"id":7})");
  const auto output = *utils::Jv::Parse(R"({"name":"pet"})");
  const auto context = *utils::Jv::Parse(R"({"token":"abc"})");

  auto result = engine.Evaluate(
      Value{"${ { before: $input, after: $output, token: $context.token } }"}, kTaskOutput, output,
      ExpressionArguments{.context = &context, .input = &input, .output = &output});
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(::strij::openworkflow::ValueToJv(*result).Dump(),
            R"({"after":{"name":"pet"},"before":{"id":7},"token":"abc"})");
}

TEST(ExpressionEngineTest, TaskExportSlotRunsAgainstShapedOutput) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  const auto input = *utils::Jv::Parse(R"({"id":7})");
  const auto output = *utils::Jv::Parse(R"({"token":"abc"})");
  const auto context = *utils::Jv::Parse(R"({"previous":true})");

  auto result = engine.Evaluate(
      Value{"${ $context + { token: .token, before: $input.id, after: $output.token } }"},
      kTaskExport, output,
      ExpressionArguments{.context = &context, .input = &input, .output = &output});
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(::strij::openworkflow::ValueToJv(*result).Dump(),
            R"({"after":"abc","before":7,"previous":true,"token":"abc"})");
}

TEST(ExpressionEngineTest, UndeclaredDescriptorArgumentIsRejected) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  auto subject = utils::Jv::Parse("null");
  ASSERT_TRUE(subject.ok());

  EXPECT_FALSE(engine.Validate(Value{"${ $task }"}, kIf).ok());
  EXPECT_FALSE(engine.Evaluate(Value{"${ $runtime }"}, kInputFrom, *subject).ok());
}

TEST(ExpressionEngineLocalTest, ADeclaredLocalIsVisibleAndAbsentOtherwise) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  const auto subject = *utils::Jv::Parse(R"({"name":"widget"})");
  const auto item = *utils::Jv::Parse(R"("red")");
  const ExpressionLocal local{.name_ = "item", .value_ = &item};
  const ExpressionArguments arguments{.locals = std::span(&local, 1)};

  auto visible = engine.Run("$item", subject, arguments);
  ASSERT_TRUE(visible.ok()) << visible.status().message();
  EXPECT_EQ(visible->Dump(), R"("red")");

  auto evaluated = engine.Evaluate(Value{Value::Object{{"color", Value{"${ $item }"}}}}, kValue,
                                   subject, arguments);
  ASSERT_TRUE(evaluated.ok()) << evaluated.status().message();
  EXPECT_EQ(evaluated->AsObject().at("color").AsString(), "red");

  EXPECT_FALSE(engine.Run("$item", subject).ok());
}

TEST(ExpressionEngineLocalTest, TheFirstLocalWithANameShadowsLaterOnes) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  const auto subject = *utils::Jv::Parse("null");
  const auto inner = *utils::Jv::Parse(R"("inner")");
  const auto outer = *utils::Jv::Parse(R"("outer")");
  const std::vector<ExpressionLocal> locals{
      {.name_ = "item", .value_ = &inner},
      {.name_ = "item", .value_ = &outer},
  };

  auto result = engine.Run("$item", subject,
                           ExpressionArguments{.locals = std::span(locals.data(), locals.size())});
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result->Dump(), R"("inner")");
}

TEST(ExpressionEngineForSlotsTest, ForInAndWhileRequireWrappedExpressions) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  for (const ExpressionSlot& slot : {kForIn, kForWhile}) {
    EXPECT_FALSE(engine.Validate(Value{".colors"}, slot).ok());
    EXPECT_FALSE(engine.Validate(Value{"${ .colors( }"}, slot).ok());
    EXPECT_TRUE(engine.Validate(Value{"${ .colors }"}, slot).ok());
    EXPECT_FALSE(engine.Validate(Value{"${ $input }"}, slot).ok());
    EXPECT_FALSE(engine.Validate(Value{"${ $output }"}, slot).ok());
  }
}

TEST(ExpressionEngineForSlotsTest, ForSlotsBindLocalsButNotInputOrOutput) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  const auto subject = *utils::Jv::Parse(R"({"colors":["red"]})");
  const auto color = *utils::Jv::Parse(R"("red")");
  const ExpressionLocal local{.name_ = "color", .value_ = &color};
  const ExpressionArguments arguments{.locals = std::span(&local, 1)};

  auto collection = engine.Evaluate(Value{"${ [.colors[], $color] }"}, kForIn, subject, arguments);
  ASSERT_TRUE(collection.ok()) << collection.status().message();
  EXPECT_EQ(::strij::openworkflow::ValueToJv(*collection).Dump(), R"(["red","red"])");

  auto predicate = engine.Evaluate(Value{"${ $color == \"red\" }"}, kForWhile, subject, arguments);
  ASSERT_TRUE(predicate.ok()) << predicate.status().message();
  EXPECT_EQ(::strij::openworkflow::ValueToJv(*predicate).Dump(), "true");

  auto non_boolean = engine.Evaluate(Value{"${ 1 }"}, kForWhile, subject, arguments);
  ASSERT_TRUE(non_boolean.ok()) << non_boolean.status().message();
  EXPECT_EQ(::strij::openworkflow::ValueToJv(*non_boolean).Dump(), "1");

  EXPECT_TRUE(engine.Validate(Value{"${ $color }"}, kForIn, arguments).ok());
}

TEST(LoopVariableNormalizationTest, DefaultsAndBothSpellingsNormalizeToBareNames) {
  auto defaults = NormalizeLoopVariables(std::nullopt, std::nullopt);
  ASSERT_TRUE(defaults.ok()) << defaults.status().message();
  EXPECT_EQ(defaults->each_, "item");
  EXPECT_EQ(defaults->at_, "index");

  auto empty = NormalizeLoopVariables(std::string{"   "}, std::string{"  "});
  ASSERT_TRUE(empty.ok()) << empty.status().message();
  EXPECT_EQ(empty->each_, "item");
  EXPECT_EQ(empty->at_, "index");

  auto plain = NormalizeLoopVariables(std::string{" color "}, std::string{"position"});
  ASSERT_TRUE(plain.ok()) << plain.status().message();
  EXPECT_EQ(plain->each_, "color");
  EXPECT_EQ(plain->at_, "position");

  auto prefixed = NormalizeLoopVariables(std::string{" $color "}, std::string{"$position"});
  ASSERT_TRUE(prefixed.ok()) << prefixed.status().message();
  EXPECT_EQ(prefixed->each_, "color");
  EXPECT_EQ(prefixed->at_, "position");
}

TEST(LoopVariableNormalizationTest, RejectsInvalidDuplicateAndReservedNames) {
  EXPECT_FALSE(NormalizeLoopVariables(std::string{"9bad"}, std::nullopt).ok());
  EXPECT_FALSE(NormalizeLoopVariables(std::string{"has-dash"}, std::nullopt).ok());
  EXPECT_FALSE(NormalizeLoopVariables(std::string{"$"}, std::nullopt).ok());
  EXPECT_FALSE(NormalizeLoopVariables(std::string{"$$item"}, std::nullopt).ok());
  EXPECT_FALSE(NormalizeLoopVariables(std::string{"same"}, std::string{"$same"}).ok());

  for (std::string_view reserved : {kContextVariable, kInputVariable, kOutputVariable}) {
    EXPECT_FALSE(NormalizeLoopVariables(std::string{reserved}, std::nullopt).ok());
    EXPECT_FALSE(NormalizeLoopVariables(std::nullopt, std::string{reserved}).ok());
  }
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
