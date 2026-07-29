#pragma once

#include <cstdint>

#include "carrot/common/pure.hh"

namespace carrot::event {

class Completable {
public:
  Completable() = default;
  virtual ~Completable() = default;

  Completable(const Completable&) = delete;
  auto operator=(const Completable&) -> Completable& = delete;
  Completable(Completable&&) noexcept = delete;
  auto operator=(Completable&&) noexcept -> Completable& = delete;

  virtual void HandleCompletion(uint8_t tag, int res, uint32_t flags) PURE;
};

} // namespace carrot::event
