#include "common/loaders/evaluator_loader.hh"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "google/protobuf/any.pb.h"
#include "strij/extensions/extension_registry.hh"

namespace strij::loaders {
namespace {

auto createEvaluatorFromExtension(const config::ExtensionConfig& ext, std::string_view source,
                                  std::vector<std::string> variable_names,
                                  const extensions::evaluators::EvaluatorDeps& deps)
    -> absl::StatusOr<extensions::evaluators::EvaluatorPtr> {
  auto& registry = extensions::Registry<extensions::evaluators::EvaluatorFactory>::instance();
  auto* factory = registry.GetFactory(ext.name());
  if (factory == nullptr) {
    const auto names = registry.GetRegisteredNames();
    return absl::NotFoundError(
        absl::StrCat("Evaluator '", ext.name(),
                     "' is not registered. Registered: ", absl::StrJoin(names, ", ")));
  }

  auto config_msg = factory->CreateEmptyConfigProto();
  // Tolerate an evaluator without a packed typed_config (the jq factory ships
  // no serialized payload): the factory defaults apply.
  if (!ext.typed_config().type_url().empty()) {
    ::google::protobuf::Any unpacked;
    unpacked.CopyFrom(ext.typed_config());
    if (!unpacked.UnpackTo(config_msg.get())) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to unpack typed_config for evaluator '", ext.name(),
                       "': unknown type '", unpacked.type_url(), "'"));
    }
  }

  auto evaluator = factory->Compile(*config_msg, source, std::move(variable_names), deps);
  // A factory returning OkStatus with a null evaluator reports an invalid
  // configuration; surface it rather than installing a null evaluator.
  if (evaluator.ok() && *evaluator == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Evaluator factory '", ext.name(), "' returned no instance (Compile returned null)"));
  }
  return evaluator;
}

} // namespace

auto CreateEvaluator(const config::ExtensionConfig& config,
                     const extensions::evaluators::EvaluatorDeps& deps, std::string_view source,
                     std::vector<std::string> variable_names)
    -> absl::StatusOr<extensions::evaluators::EvaluatorPtr> {
  return createEvaluatorFromExtension(config, source, std::move(variable_names), deps);
}

} // namespace strij::loaders