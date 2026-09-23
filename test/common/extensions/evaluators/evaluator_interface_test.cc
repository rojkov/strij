#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "common/config/extensions.pb.h"
#include "common/core/utils/jv.hh"
#include "common/extensions/evaluators/evaluator.hh"
#include "gtest/gtest.h"
#include "strij/extensions/extension_registry.hh"

namespace strij::extensions::evaluators {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

// Simulates an evaluator that copies arg values then the input as outputs; the
// compiled source is replayed verbatim through Run's outputs so a test can
// observe that variable_names/args alignment survives the round-trip.
class FakeEvaluator : public Evaluator {
public:
  explicit FakeEvaluator(std::string source) : source_(std::move(source)) {}

  auto Run(const utils::Jv& input, std::span<const utils::Jv> args)
      -> absl::StatusOr<std::vector<utils::Jv>> override {
    std::vector<utils::Jv> outputs;
    for (const auto& arg : args) {
      outputs.push_back(arg.Copy());
    }
    outputs.push_back(input.Copy());
    outputs.push_back(utils::Jv::Parse(R"(")" + source_ + R"(")").value());
    return outputs;
  }

private:
  std::string source_;
};

class FakeEvaluatorFactory : public EvaluatorFactory {
public:
  auto Name() const -> std::string override { return "fake_eval"; }
  auto CreateEmptyConfigProto() -> MessagePtr override {
    return std::make_unique<config::ExtensionConfig>();
  }
  auto Compile(const ::google::protobuf::Message& /*config*/, std::string_view source,
               std::vector<std::string> /*variable_names*/, const EvaluatorDeps& /*deps*/)
      -> absl::StatusOr<EvaluatorPtr> override {
    if (source == "syntax error") {
      return absl::InvalidArgumentError("fake: " + std::string(source));
    }
    return std::make_unique<FakeEvaluator>(std::string(source));
  }
};

REGISTER_FACTORY(FakeEvaluatorFactory, EvaluatorFactory)

TEST(EvaluatorInterfaceTest, FakeFactoryIsRegisteredUnderName) {
  auto& registry = Registry<EvaluatorFactory>::instance();
  auto names = registry.GetRegisteredNames();
  EXPECT_NE(std::find(names.begin(), names.end(), "fake_eval"), names.end());
  EXPECT_NE(registry.GetFactory("fake_eval"), nullptr);
}

TEST(EvaluatorInterfaceTest, CompileThenRunYieldsOutputs) {
  auto& registry = Registry<EvaluatorFactory>::instance();
  auto* factory = registry.GetFactory("fake_eval");
  ASSERT_NE(factory, nullptr);

  EvaluatorDeps deps;
  auto evaluator = factory->Compile(*factory->CreateEmptyConfigProto(), ".input", {"x"}, deps);
  ASSERT_TRUE(evaluator.ok()) << evaluator.status().message();

  utils::Jv input = utils::Jv::Parse(R"({"a":1})").value();
  utils::Jv arg = utils::Jv::Parse("7").value();
  std::vector<utils::Jv> args;
  args.push_back(arg.Copy());
  auto outputs = (*evaluator)->Run(input, args);
  ASSERT_TRUE(outputs.ok()) << outputs.status().message();
  ASSERT_EQ(outputs->size(), 3);
  EXPECT_EQ((*outputs)[0].Dump(), "7");
  EXPECT_EQ((*outputs)[1].Dump(), R"({"a":1})");
  EXPECT_EQ((*outputs)[2].Dump(), R"(".input")");
}

TEST(EvaluatorInterfaceTest, CompileErrorSurfacesAsStatus) {
  auto& registry = Registry<EvaluatorFactory>::instance();
  auto* factory = registry.GetFactory("fake_eval");
  ASSERT_NE(factory, nullptr);

  EvaluatorDeps deps;
  auto evaluator = factory->Compile(*factory->CreateEmptyConfigProto(), "syntax error", {}, deps);
  ASSERT_FALSE(evaluator.ok());
  EXPECT_EQ(evaluator.status().code(), absl::StatusCode::kInvalidArgument);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::extensions::evaluators