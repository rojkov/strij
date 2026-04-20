#pragma once

#include <memory>

#include "carrot/common/pure.hh"
#include "carrot/event/command.hh"

namespace carrot {
namespace event {

class IOObject {
public:
  IOObject() = default;
  virtual ~IOObject() = default;

  IOObject(const IOObject&) = delete;
  IOObject& operator=(const IOObject&) = delete;
  IOObject(IOObject&&) noexcept = delete;
  IOObject& operator=(IOObject&&) noexcept = delete;

  virtual void HandleCompletion(int res, uint32_t flags) PURE;
  virtual void ProcessCommand(Command cmd) PURE;
};

using IOObjectPtr = std::unique_ptr<IOObject>;

} // namespace event
} // namespace carrot
