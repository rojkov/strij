#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "common/config/extensions.pb.h"
#include "common/core/utils/jv.hh"
#include "common/extensions/evaluators/evaluator.hh"
#include "common/extensions/evaluators/jq/jq.pb.h"
#include "common/loaders/evaluator_loader.hh"
#include "gtest/gtest.h"
#include "strij/extensions/extension_registry.hh"

namespace strij::loaders {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

// Second registered evaluator so the loader test can pin one language name to
// two different factories.
class PinEvaluator : public extensions::evaluators::Evaluator {
public:
  auto Run(const utils::Jv& input, std::span<const utils::Jv> /*args*/)
      -> absl::StatusOr<std::vector<utils::Jv>> override {
    std::vector<utils::Jv> outputs;
    outputs.push_back(input.Copy());
    return outputs;
  }
};

class PinEvaluatorFactory : public extensions::evaluators::EvaluatorFactory {
public:
  auto Name() const -> std::string override { return "fake_eval"; }
  auto CreateEmptyConfigProto() -> MessagePtr override {
    return std::make_unique<config::ExtensionConfig>();
  }
  auto Compile(const ::google::protobuf::Message& /*config*/, std::string_view /*source*/,
               std::vector<std::string> /*variable_names*/,
               const extensions::evaluators::EvaluatorDeps& /*deps*/)
      -> absl::StatusOr<extensions::evaluators::EvaluatorPtr> override {
    return std::make_unique<PinEvaluator>();
  }
};

REGISTER_FACTORY_FULLY_QUALIFIED(strij::loaders::PinEvaluatorFactory,
                                 strij::extensions::evaluators::EvaluatorFactory,
                                 pin_evaluator_registrar)

TEST(EvaluatorLoaderTest, EmptyEvaluatorNameIsRejected) {
  config::ExtensionConfig config;
  extensions::evaluators::EvaluatorDeps deps;

  auto result = CreateEvaluator(config, deps, ".", {});
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
  EXPECT_NE(result.status().message().find("Evaluator"), std::string::npos);
}

TEST(EvaluatorLoaderTest, UnknownEvaluatorNameIsRejected) {
  config::ExtensionConfig config;
  config.set_name("nonexistent");
  extensions::evaluators::EvaluatorDeps deps;

  auto result = CreateEvaluator(config, deps, ".", {});
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
  EXPECT_NE(result.status().message().find("nonexistent"), std::string::npos);
}

TEST(EvaluatorLoaderTest, CreatesRegisteredEvaluatorWithoutTypedConfig) {
  config::ExtensionConfig config;
  config.set_name("jq");
  extensions::evaluators::EvaluatorDeps deps;

  auto result = CreateEvaluator(config, deps, "$name", {"name"});
  ASSERT_TRUE(result.ok()) << result.status().message();

  std::vector<utils::Jv> arg;
  arg.push_back(utils::Jv::Parse(R"("loaded")").value());
  auto outputs = (*result)->Run(utils::Jv::Parse("null").value(), arg);
  ASSERT_TRUE(outputs.ok()) << outputs.status().message();
  ASSERT_EQ(outputs->size(), static_cast<size_t>(1));
  EXPECT_EQ((*outputs)[0].Dump(), "\"loaded\"");
}

TEST(EvaluatorLoaderTest, UnpacksTypedConfig) {
  config::ExtensionConfig config;
  config.set_name("jq");
  config.mutable_typed_config()->PackFrom(
      strij::extensions::evaluators::jq::JqEvaluatorConfig());
  extensions::evaluators::EvaluatorDeps deps;

  auto result = CreateEvaluator(config, deps, ".x", {});
  ASSERT_TRUE(result.ok()) << result.status().message();
}

TEST(EvaluatorLoaderTest, MisconfiguredTypedConfigIsRejected) {
  config::ExtensionConfig inner;
  inner.set_name("jq");
  inner.mutable_typed_config()->PackFrom(config::ExtensionConfig());

  config::ExtensionConfig config;
  config.set_name("jq");
  config.mutable_typed_config()->PackFrom(inner);
  extensions::evaluators::EvaluatorDeps deps;

  auto result = CreateEvaluator(config, deps, ".", {});
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(result.status().message().find("Failed to unpack"), std::string::npos);
}

TEST(EvaluatorLoaderTest, PinsOneLanguageToEachConfiguredName) {
  extensions::evaluators::EvaluatorDeps deps;
  config::ExtensionConfig jq_config;
  jq_config.set_name("jq");
  config::ExtensionConfig fake_config;
  fake_config.set_name("fake_eval");

  auto jq = CreateEvaluator(jq_config, deps, ".name", {});
  ASSERT_TRUE(jq.ok()) << jq.status().message();
  auto fake = CreateEvaluator(fake_config, deps, ".unused", {});
  ASSERT_TRUE(fake.ok()) << fake.status().message();

  auto jq_outputs = (*jq)->Run(utils::Jv::Parse(R"({"name":"x"})").value(), {});
  ASSERT_TRUE(jq_outputs.ok()) << jq_outputs.status().message();
  EXPECT_EQ((*jq_outputs)[0].Dump(), "\"x\"");

  auto fake_outputs = (*fake)->Run(utils::Jv::Parse("5").value(), {});
  ASSERT_TRUE(fake_outputs.ok()) << fake_outputs.status().message();
  EXPECT_EQ((*fake_outputs)[0].Dump(), "5");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::loaders