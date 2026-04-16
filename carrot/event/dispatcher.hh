#pragma once

#include <memory>

#include "carrot/common/pure.hh"

namespace carrot {
namespace event {

class Dispatcher {
public:
  Dispatcher() = default;
  virtual ~Dispatcher() = default;

  Dispatcher(const Dispatcher&) = delete;
  Dispatcher& operator=(const Dispatcher&) = delete;
  Dispatcher(Dispatcher&&) noexcept = delete;
  Dispatcher& operator=(Dispatcher&&) noexcept = delete;

  virtual void Run() PURE;
};

using DispatcherPtr = std::unique_ptr<Dispatcher>;

} // namespace event
} // namespace carrot
