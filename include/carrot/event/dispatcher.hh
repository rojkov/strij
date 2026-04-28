#pragma once

#include <memory>
#include <span>

#include "carrot/common/pure.hh"
#include "carrot/event/command.hh"
#include "liburing.h"

namespace carrot::event {

class Dispatcher {
public:
  Dispatcher() = default;
  virtual ~Dispatcher() = default;

  Dispatcher(const Dispatcher&) = delete;
  auto operator=(const Dispatcher&) -> Dispatcher& = delete;
  Dispatcher(Dispatcher&&) noexcept = delete;
  auto operator=(Dispatcher&&) noexcept -> Dispatcher& = delete;

  [[nodiscard]] virtual auto GetRing() -> struct io_uring* PURE;
  virtual void Run() PURE;
  virtual void Shutdown() PURE;
  virtual void SubmitCommand(Command cmd) PURE;
  virtual void PrepareRead(IOObject* io_object, int fd, std::span<std::byte> buf,
                           off_t offset) PURE;
};

using DispatcherSharedPtr = std::shared_ptr<Dispatcher>;

} // namespace carrot::event
