#pragma once

#include <cstdint>
#include <memory>

#include "carrot/common/pure.hh"
#include "carrot/event/command.hh"

namespace carrot::event {

class IOObject {
public:
  IOObject() = default;
  virtual ~IOObject() = default;

  IOObject(const IOObject&) = delete;
  auto operator=(const IOObject&) -> IOObject& = delete;
  IOObject(IOObject&&) noexcept = delete;
  auto operator=(IOObject&&) noexcept -> IOObject& = delete;

  /**
   * @brief A hook called by `Dispatcher` upon completion of a system call.
   * @param tag An operation tag interpreted by an IOObject subclass.
   */
  virtual void HandleCompletion(uint8_t tag, int res, uint32_t flags) PURE;
  virtual void ProcessCommand(Command cmd) PURE;
};

using IOObjectPtr = std::unique_ptr<IOObject>;

} // namespace carrot::event
