#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "common/core/utils/jv.hh"
#include "google/protobuf/message.h"
#include "strij/common/pure.hh"

namespace strij::extensions::evaluators {

class Evaluator;
using EvaluatorPtr = std::unique_ptr<Evaluator>;

// A compiled expression program, shared by gateway and nodeagent consumers
// (see spec expression-evaluation). One instance == one compiled source; Run()
// executes it against JSON inputs, binding named variables at run time.
class Evaluator {
public:
  Evaluator() = default;
  virtual ~Evaluator() = default;

  Evaluator(const Evaluator&) = delete;
  auto operator=(const Evaluator&) -> Evaluator& = delete;
  Evaluator(Evaluator&&) noexcept = delete;
  auto operator=(Evaluator&&) noexcept -> Evaluator& = delete;

  // Runs the compiled program against `input`. `args` is positionally aligned
  // with the variable_names passed to Compile(): the i-th arg binds the i-th
  // declared variable. Returns every output value the program produces; an
  // error (e.g. jq runtime/compile failure) carries the engine's message. A
  // failed run must not corrupt later runs.
  [[nodiscard]] virtual auto Run(const utils::Jv& input, std::span<const utils::Jv> args)
      -> absl::StatusOr<std::vector<utils::Jv>> PURE;
};

// Framework-facing bundle passed to every evaluator factory. Empty for v1 (jq
// needs nothing from the framework); kept as a bundle so the Compile signature
// is uniform with the scheduler/fetcher categories and can grow (e.g. a
// Dispatcher&) without breaking callers, and passed const& like the other
// category deps bundles.
struct EvaluatorDeps {
  EvaluatorDeps() = default;
  ~EvaluatorDeps() = default;

  EvaluatorDeps(const EvaluatorDeps&) = delete;
  auto operator=(const EvaluatorDeps&) -> EvaluatorDeps& = delete;
  EvaluatorDeps(EvaluatorDeps&&) noexcept = delete;
  auto operator=(EvaluatorDeps&&) noexcept -> EvaluatorDeps& = delete;
};

// Evaluator factory, registered in extensions::Registry<EvaluatorFactory>.
// Mirrors the gateway/nodeagent scheduler-factory shape: Name, an empty config
// proto to dial the config type, and the creation method (here Compile, since
// an evaluator needs an expression source in addition to a config).
class EvaluatorFactory {
public:
  using MessagePtr = std::unique_ptr<::google::protobuf::Message>;

  EvaluatorFactory() = default;
  virtual ~EvaluatorFactory() = default;

  EvaluatorFactory(const EvaluatorFactory&) = delete;
  auto operator=(const EvaluatorFactory&) -> EvaluatorFactory& = delete;
  EvaluatorFactory(EvaluatorFactory&&) noexcept = delete;
  auto operator=(EvaluatorFactory&&) noexcept -> EvaluatorFactory& = delete;

  [[nodiscard]] virtual auto Name() const -> std::string PURE;
  virtual auto CreateEmptyConfigProto() -> MessagePtr PURE;
  // Compiles `source` into an evaluator. `variable_names` declares the named
  // variables the expression may reference, in argument-alignment order; they
  // are bound per Run() invocation. Returns an error (with the engine's
  // message) when the source does not compile; on failure no instance is left
  // half-constructed, and a null EvaluatorPtr is a config error.
  [[nodiscard]] virtual auto
  Compile(const ::google::protobuf::Message& config, std::string_view source,
          std::vector<std::string> variable_names, const EvaluatorDeps& deps)
      -> absl::StatusOr<EvaluatorPtr> PURE;
};

} // namespace strij::extensions::evaluators