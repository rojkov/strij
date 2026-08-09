#pragma once

#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "strij/common/pure.hh"

namespace strij::extensions {

// Well-known task parameter key that names the executable a task wants to run.
// Populated at the gateway from the `x-strij-function` request header. All
// function-consuming task handlers read it via this constant.
inline constexpr std::string_view kFunctionParameter = "function";

/**
 * @brief Resolves a `function` task parameter into an executable path.
 *
 * Shared nodeagent infrastructure: every function-consuming task handler
 * obtains its resolver through FactoryContext instead of touching the
 * filesystem directly. v1 uses LocalFunctionResolver (the reference is the
 * path); the deployment extension swaps in a cache-backed resolver.
 */
class FunctionResolver {
public:
  FunctionResolver() = default;
  virtual ~FunctionResolver() = default;

  FunctionResolver(const FunctionResolver&) = delete;
  auto operator=(const FunctionResolver&) -> FunctionResolver& = delete;
  FunctionResolver(FunctionResolver&&) noexcept = delete;
  auto operator=(FunctionResolver&&) noexcept -> FunctionResolver& = delete;

  /**
   * @brief Resolves a function reference into the path of the executable to
   * run.
   *
   * Returns the reference unchanged as the executable path; an empty reference
   * is an error.
   */
  virtual auto Resolve(std::string_view reference) -> absl::StatusOr<std::string> PURE;
};

// TODO: this class conceptually belongs to strij::nodeagent namespace, but resides in
// src/core/extensions. Consider moving it to src/nodeagent.
class LocalFunctionResolver final : public FunctionResolver {
public:
  // FunctionResolver
  auto Resolve(std::string_view reference) -> absl::StatusOr<std::string> override;
};

} // namespace strij::extensions
