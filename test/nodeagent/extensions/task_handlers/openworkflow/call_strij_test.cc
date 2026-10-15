#include "nodeagent/extensions/task_handlers/openworkflow/call_strij.hh"

#include <cstdint>
#include <string>

#include "common/core/utils/jv.hh"
#include "gtest/gtest.h"
#include "openworkflow/document.hh"
#include "openworkflow/use.hh"
#include "test/nodeagent/extensions/task_handlers/openworkflow/fakes.hh"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

using ::strij::openworkflow::Document;
using ::strij::openworkflow::Value;

// NOLINTBEGIN(modernize-use-trailing-return-type)

auto MakeDocument() -> Document {
  Document document;
  document.use_.emplace();
  return document;
}

auto With(std::initializer_list<std::pair<const std::string, Value>> members) -> Value {
  return Value{Value::Object{members}};
}

TEST(ClassifyCallTargetTest, DistinguishesAllFourKinds) {
  const Document document = MakeDocument();
  EXPECT_EQ(ClassifyCallTarget("strij", document), CallTargetKind::kStrij);
  EXPECT_EQ(ClassifyCallTarget("http", document), CallTargetKind::kReserved);
  EXPECT_EQ(ClassifyCallTarget("openapi", document), CallTargetKind::kReserved);
  EXPECT_EQ(ClassifyCallTarget("asyncapi", document), CallTargetKind::kReserved);
  EXPECT_EQ(ClassifyCallTarget("grpc", document), CallTargetKind::kReserved);
  EXPECT_EQ(ClassifyCallTarget("mcp", document), CallTargetKind::kReserved);
  EXPECT_EQ(ClassifyCallTarget("petstore", document), CallTargetKind::kUnknown);
}

TEST(ClassifyCallTargetTest, DeclaredFunctionIsDistinct) {
  Document document = MakeDocument();
  document.use_->functions_.emplace("getPet", ::strij::openworkflow::Task{});
  EXPECT_EQ(ClassifyCallTarget("getPet", document), CallTargetKind::kDeclaredFunction);
}

TEST(ValidateCallArgumentsTest, AcceptsTheThreeKeys) {
  const Value with = With({
      {"type", Value{"echo"}},
      {"parameters", Value{Value::Object{{"function", Value{"/bin/cat"}}}}},
      {"requirements", Value{Value::Object{{"cpu", Value{std::int64_t{2}}}}}},
  });
  EXPECT_TRUE(ValidateCallArguments(with).ok());
}

TEST(ValidateCallArgumentsTest, RejectsUnknownKeyNamingIt) {
  const Value with = With({
      {"type", Value{"echo"}},
      {"bogus", Value{"x"}},
  });
  auto status = ValidateCallArguments(with);
  ASSERT_FALSE(status.ok());
  EXPECT_NE(status.message().find("bogus"), std::string_view::npos);
  EXPECT_NE(status.message().find("type"), std::string_view::npos);
  EXPECT_NE(status.message().find("parameters"), std::string_view::npos);
  EXPECT_NE(status.message().find("requirements"), std::string_view::npos);
}

TEST(ValidateCallArgumentsTest, RequiresType) {
  const Value with = With({{"parameters", Value{Value::Object{}}}});
  auto status = ValidateCallArguments(with);
  ASSERT_FALSE(status.ok());
  EXPECT_NE(status.message().find("type"), std::string_view::npos);
}

TEST(ValidateCallArgumentsTest, RejectsExpressionType) {
  const Value with = With({{"type", Value{"${ .x }"}}});
  auto status = ValidateCallArguments(with);
  ASSERT_FALSE(status.ok());
  EXPECT_NE(status.message().find("type"), std::string_view::npos);
  EXPECT_NE(status.message().find("expression"), std::string_view::npos);
}

TEST(ValidateCallArgumentsTest, RejectsExpressionRequirement) {
  const Value with = With({
      {"type", Value{"echo"}},
      {"requirements", Value{Value::Object{{"cpu", Value{"${ .x }"}}}}},
  });
  auto status = ValidateCallArguments(with);
  ASSERT_FALSE(status.ok());
  EXPECT_NE(status.message().find("cpu"), std::string_view::npos);
  EXPECT_NE(status.message().find("expression"), std::string_view::npos);
}

TEST(CoerceRequirementTest, AcceptsNonNegativeIntegers) {
  auto zero = CoerceRequirement("cpu", Value{std::int64_t{0}});
  ASSERT_TRUE(zero.ok());
  EXPECT_EQ(*zero, 0U);
  auto many = CoerceRequirement("cpu", Value{std::int64_t{16}});
  ASSERT_TRUE(many.ok());
  EXPECT_EQ(*many, 16U);
}

TEST(CoerceRequirementTest, RejectsFractional) {
  auto status = CoerceRequirement("cpu", Value{1.5});
  ASSERT_FALSE(status.ok());
  EXPECT_NE(status.status().message().find("cpu"), std::string_view::npos);
}

TEST(CoerceRequirementTest, RejectsNegative) {
  auto status = CoerceRequirement("cpu", Value{std::int64_t{-1}});
  ASSERT_FALSE(status.ok());
  EXPECT_NE(status.status().message().find("non-negative"), std::string_view::npos);
}

TEST(CoerceRequirementTest, RejectsUnitSuffixWithoutTruncating) {
  auto status = CoerceRequirement("mem", Value{std::string{"100M"}});
  ASSERT_FALSE(status.ok());
  EXPECT_NE(status.status().message().find("100M"), std::string_view::npos);
  EXPECT_NE(status.status().message().find("unit suffixes"), std::string_view::npos);
}

TEST(CoerceRequirementTest, RejectsOutOfRange) {
  auto status = CoerceRequirement("mem", Value{1e30});
  ASSERT_FALSE(status.ok());
  EXPECT_NE(status.status().message().find("out of range"), std::string_view::npos);
}

TEST(CoerceRequirementTest, RejectsNonNumeric) {
  auto status = CoerceRequirement("mem", Value{std::string{"abc"}});
  ASSERT_FALSE(status.ok());
  EXPECT_NE(status.status().message().find("mem"), std::string_view::npos);
  EXPECT_NE(status.status().message().find("abc"), std::string_view::npos);
}

TEST(ProjectCallTest, ProjectsTypeParametersAndRequirements) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  auto subject = utils::Jv::Parse("null");
  ASSERT_TRUE(subject.ok());

  const Value with = With({
      {"type", Value{"piped_executable"}},
      {"parameters", Value{Value::Object{{"function", Value{"/bin/cat"}}}}},
      {"requirements", Value{Value::Object{{"cpu", Value{std::int64_t{2}}}}}},
  });
  auto projection = ProjectCall(with, &*subject, engine);
  ASSERT_TRUE(projection.ok()) << projection.status().message();
  EXPECT_EQ(projection->type_, "piped_executable");
  EXPECT_EQ(projection->parameters_.at("function"), "/bin/cat");
  EXPECT_EQ(projection->requirements_.resources().at("cpu"), 2U);
}

TEST(ProjectCallTest, EvaluatesParameterExpressionUnquoted) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  auto subject = utils::Jv::Parse(R"({"name":"widget"})");
  ASSERT_TRUE(subject.ok());

  const Value with = With({
      {"type", Value{"echo"}},
      {"parameters", Value{Value::Object{{"p", Value{"${ .name }"}}}}},
  });
  auto projection = ProjectCall(with, &*subject, engine);
  ASSERT_TRUE(projection.ok()) << projection.status().message();
  EXPECT_EQ(projection->parameters_.at("p"), "widget");
}

TEST(ProjectCallTest, RendersNumericParameterCanonically) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  auto subject = utils::Jv::Parse(R"({"n":41})");
  ASSERT_TRUE(subject.ok());

  const Value with = With({
      {"type", Value{"echo"}},
      {"parameters", Value{Value::Object{{"p", Value{"${ .n + 1 }"}}}}},
  });
  auto projection = ProjectCall(with, &*subject, engine);
  ASSERT_TRUE(projection.ok()) << projection.status().message();
  EXPECT_EQ(projection->parameters_.at("p"), "42");
}

TEST(ProjectCallTest, RejectsNullParameterExpression) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  auto subject = utils::Jv::Parse(R"({"name":"widget"})");
  ASSERT_TRUE(subject.ok());

  const Value with = With({
      {"type", Value{"echo"}},
      {"parameters", Value{Value::Object{{"p", Value{"${ .missing }"}}}}},
  });
  EXPECT_FALSE(ProjectCall(with, &*subject, engine).ok());
}

TEST(ProjectCallTest, RejectsNonScalarParameterExpression) {
  ExpressionEngine engine(test::JqEvaluatorConfig());
  auto subject = utils::Jv::Parse(R"({"name":"widget"})");
  ASSERT_TRUE(subject.ok());

  const Value with = With({
      {"type", Value{"echo"}},
      {"parameters", Value{Value::Object{{"p", Value{"${ . }"}}}}},
  });
  EXPECT_FALSE(ProjectCall(with, &*subject, engine).ok());
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
