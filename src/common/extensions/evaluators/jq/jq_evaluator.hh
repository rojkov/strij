#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "common/core/utils/jv.hh"
#include "common/extensions/evaluators/evaluator.hh"
#include "google/protobuf/message.h"
#include "strij/common/pure.hh"

namespace strij::extensions::evaluators {

// The reference jq evaluator (registered name "jq", see spec
// expression-evaluation). One instance is one compiled expression source: it
// captures the source and the declared variable order from Compile, and each
// Run re-compiles with the caller's per-run argument values (libjq binds jq
// variables at compile time; see design.md D4). Immutable after construction,
// so a compiled handle is safe to share across threads.
class JqEvaluator : public Evaluator {
public:
  JqEvaluator(std::string source, std::vector<std::string> variable_names);
  ~JqEvaluator() override = default;

  JqEvaluator(const JqEvaluator&) = delete;
  auto operator=(const JqEvaluator&) -> JqEvaluator& = delete;
  JqEvaluator(JqEvaluator&&) noexcept = delete;
  auto operator=(JqEvaluator&&) noexcept -> JqEvaluator& = delete;

  auto Run(const utils::Jv& input, std::span<const utils::Jv> args)
      -> absl::StatusOr<std::vector<utils::Jv>> override;

private:
  std::string source_;
  std::vector<std::string> variable_names_;
};

class JqEvaluatorFactory : public EvaluatorFactory {
public:
  auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Compile(const ::google::protobuf::Message& /*config*/, std::string_view source,
               std::vector<std::string> variable_names, const EvaluatorDeps& /*deps*/)
      -> absl::StatusOr<EvaluatorPtr> override;
};

} // namespace strij::extensions::evaluators