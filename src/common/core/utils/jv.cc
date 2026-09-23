#include "common/core/utils/jv.hh"

#include <string>

#include "absl/status/status.h"

namespace strij::utils {

Jv::Jv() : value_{jv_invalid()} {}
Jv::~Jv() { jv_free(value_); }

Jv::Jv(jv value) : value_{value} {}

Jv::Jv(Jv&& other) noexcept : value_{other.value_} { other.value_ = jv_invalid(); }

auto Jv::operator=(Jv&& other) noexcept -> Jv& {
  if (this != &other) {
    jv_free(value_);
    value_ = other.value_;
    other.value_ = jv_invalid();
  }
  return *this;
}

auto Jv::Parse(std::string_view text) -> absl::StatusOr<Jv> {
  jv parsed = jv_parse(std::string(text).c_str());
  if (jv_is_valid(parsed) == 0) {
    jv_free(parsed);
    return absl::InvalidArgumentError("invalid JSON: " + std::string(text));
  }
  return Jv(parsed);
}

auto Jv::Dump() const -> std::string {
  jv dumped = jv_dump_string(jv_copy(value_), 0);
  std::string result(jv_string_value(dumped), jv_string_length_bytes(jv_copy(dumped)));
  jv_free(dumped);
  return result;
}

auto Jv::IsValid() const -> bool { return jv_is_valid(value_) == 1; }

auto Jv::Copy() const -> Jv { return Jv(jv_copy(value_)); }

auto Acquire(jv value) -> Jv { return Jv(value); }

auto JvRawCopy(const Jv& value) -> jv { return jv_copy(value.value_); }

} // namespace strij::utils