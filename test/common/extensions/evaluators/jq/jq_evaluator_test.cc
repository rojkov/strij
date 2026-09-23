#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "common/core/utils/jv.hh"
#include "common/extensions/evaluators/evaluator.hh"
#include "common/extensions/evaluators/jq/jq_evaluator.hh"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "strij/extensions/extension_registry.hh"

namespace strij::extensions::evaluators {
namespace {

using ::testing::NotNull;

// NOLINTBEGIN(modernize-use-trailing-return-type)

class JqEvaluatorTest : public ::testing::Test {
protected:
  void SetUp() override {
    factory_ = Registry<EvaluatorFactory>::instance().GetFactory("jq");
    ASSERT_THAT(factory_, NotNull());
  }

  auto compile(std::string source, std::vector<std::string> variable_names = {})
      -> absl::StatusOr<EvaluatorPtr> {
    return factory_->Compile(*factory_->CreateEmptyConfigProto(), std::move(source),
                             std::move(variable_names), deps_);
  }

  EvaluatorFactory* factory_ = nullptr;
  EvaluatorDeps deps_;
};

TEST_F(JqEvaluatorTest, CompileThenRunReturnsOutput) {
  auto evaluator = compile(".name");
  ASSERT_TRUE(evaluator.ok()) << evaluator.status().message();
  auto outputs = (*evaluator)
                     ->Run(utils::Jv::Parse(R"({"name":"world"})").value(), {});
  ASSERT_TRUE(outputs.ok()) << outputs.status().message();
  ASSERT_EQ(outputs->size(), static_cast<size_t>(1));
  EXPECT_EQ((*outputs)[0].Dump(), "\"world\"");
}

TEST_F(JqEvaluatorTest, CompileErrorSurfacesWithMessage) {
  auto evaluator = compile(". | . |");
  ASSERT_FALSE(evaluator.ok());
  EXPECT_EQ(evaluator.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_FALSE(evaluator.status().message().empty());
}

TEST_F(JqEvaluatorTest, UnboundVariableIsACompileError) {
  auto evaluator = compile("$missing");
  ASSERT_FALSE(evaluator.ok());
  EXPECT_NE(evaluator.status().message().find("missing"), std::string::npos);
}

TEST_F(JqEvaluatorTest, NamedVariablesBindArgumentsAtRunTime) {
  auto evaluator = compile("$x + $y", {"x", "y"});
  ASSERT_TRUE(evaluator.ok()) << evaluator.status().message();
  std::vector<utils::Jv> args;
  args.push_back(utils::Jv::Parse("1").value());
  args.push_back(utils::Jv::Parse("2").value());
  auto outputs = (*evaluator)->Run(utils::Jv::Parse("null").value(), args);
  ASSERT_TRUE(outputs.ok()) << outputs.status().message();
  ASSERT_EQ(outputs->size(), static_cast<size_t>(1));
  EXPECT_EQ((*outputs)[0].Dump(), "3");
}

TEST_F(JqEvaluatorTest, MultiOutputGeneratorsReturnEveryOutput) {
  auto evaluator = compile(".[]");
  ASSERT_TRUE(evaluator.ok()) << evaluator.status().message();
  auto outputs =
      (*evaluator)->Run(utils::Jv::Parse("[1, 2, 3]").value(), {});
  ASSERT_TRUE(outputs.ok()) << outputs.status().message();
  ASSERT_EQ(outputs->size(), static_cast<size_t>(3));
  EXPECT_EQ((*outputs)[0].Dump(), "1");
  EXPECT_EQ((*outputs)[1].Dump(), "2");
  EXPECT_EQ((*outputs)[2].Dump(), "3");
}

TEST_F(JqEvaluatorTest, ReferenceSemantics) {
  auto evaluator = compile(".user | {name: .name, len: (.items | length), msg: \"hi \\(.name)\"}");
  ASSERT_TRUE(evaluator.ok()) << evaluator.status().message();
  auto outputs = (*evaluator)
                     ->Run(utils::Jv::Parse(R"({"user":{"name":"bob","items":[1,2]}})").value(),
                           {});
  ASSERT_TRUE(outputs.ok()) << outputs.status().message();
  ASSERT_EQ(outputs->size(), static_cast<size_t>(1));
  EXPECT_EQ((*outputs)[0].Dump(),
            R"({"name":"bob","len":2,"msg":"hi bob"})");
}

TEST_F(JqEvaluatorTest, RuntimeErrorCarriesMessageAndDoesNotCorrupt) {
  auto evaluator = compile(".foo");
  ASSERT_TRUE(evaluator.ok()) << evaluator.status().message();

  auto failing = (*evaluator)->Run(utils::Jv::Parse("42").value(), {});
  ASSERT_FALSE(failing.ok());
  EXPECT_EQ(failing.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(failing.status().message().find("Cannot index number"), std::string::npos);

  auto succeeding =
      (*evaluator)->Run(utils::Jv::Parse(R"({"foo":7})").value(), {});
  ASSERT_TRUE(succeeding.ok()) << succeeding.status().message();
  ASSERT_EQ(succeeding->size(), static_cast<size_t>(1));
  EXPECT_EQ((*succeeding)[0].Dump(), "7");
}

TEST_F(JqEvaluatorTest, JsonBoundaryRoundTrip) {
  auto evaluator = compile(".payload.items[0]");
  ASSERT_TRUE(evaluator.ok()) << evaluator.status().message();
  auto outputs = (*evaluator)
                     ->Run(utils::Jv::Parse(R"({"payload":{"items":["a","b"]}})").value(),
                           {});
  ASSERT_TRUE(outputs.ok()) << outputs.status().message();
  EXPECT_EQ((*outputs)[0].Dump(), "\"a\"");
}

TEST_F(JqEvaluatorTest, PositionalArgsAlignWithDeclaredVariables) {
  auto evaluator = compile("[$a, $b]", {"a", "b"});
  ASSERT_TRUE(evaluator.ok()) << evaluator.status().message();
  std::vector<utils::Jv> args;
  args.push_back(utils::Jv::Parse(R"({"k":"first"})").value());
  args.push_back(utils::Jv::Parse("true").value());
  auto outputs = (*evaluator)->Run(utils::Jv::Parse("null").value(), args);
  ASSERT_TRUE(outputs.ok()) << outputs.status().message();
  ASSERT_EQ(outputs->size(), static_cast<size_t>(1));
  EXPECT_EQ((*outputs)[0].Dump(), R"([{"k":"first"},true])");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::extensions::evaluators