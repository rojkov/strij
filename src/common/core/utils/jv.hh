#pragma once

#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "jv.h"

namespace strij::utils {

class Jv;

// Takes ownership of `value` (refcount already accounted for by the caller) and
// wraps it in a Jv. Internal-to-src plumbing for the evaluator category.
auto Acquire(jv value) -> Jv;

// Returns a raw libjq handle sharing `value`'s data (jv_copy: refcount bumped).
// The caller becomes the owner and MUST jv_free it. Internal-to-src plumbing.
[[nodiscard]] auto JvRawCopy(const Jv& value) -> jv;

// RAII owner of a libjq value (see design.md D2). Move-only; refcounts are
// managed here, never by consumers. Lives under src/ only: jv.h is a libjq
// header, and the public include/ surface may reference only include/, absl,
// protobuf, and std (include-purity).
class Jv {
public:
  Jv();
  ~Jv();

  Jv(const Jv&) = delete;
  auto operator=(const Jv&) -> Jv& = delete;
  Jv(Jv&&) noexcept;
  auto operator=(Jv&&) noexcept -> Jv&;

  // Parses `text` as a JSON document. Returns an error (not an exception) when
  // the input is not valid JSON.
  [[nodiscard]] static auto Parse(std::string_view text) -> absl::StatusOr<Jv>;
  // Renders the value back to JSON text.
  [[nodiscard]] auto Dump() const -> std::string;
  [[nodiscard]] auto IsValid() const -> bool;

  // A handle sharing this value's data (refcount incremented internally).
  [[nodiscard]] auto Copy() const -> Jv;

private:
  friend auto Acquire(jv value) -> Jv;
  friend auto JvRawCopy(const Jv& value) -> jv;

  explicit Jv(jv value);

  jv value_;
};

} // namespace strij::utils