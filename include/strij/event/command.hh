
#pragma once

#include <cstdint>

namespace strij::event {

class CommandHandler;

struct Command {
  // DEFERRED_DELETE: the destination (a CommandHandler owning the object in
  // args_) destroys it outside the completion stack. Mirrors the
  // Connection::onEndOfStream teardown pattern.
  //
  // CAPACITY_RELEASED: a pure wakeup that node capacity may have been
  // released. args_ is always nullptr; the receiver re-queries its sources of
  // truth (e.g. the AdmissionController) instead of reading a payload.
  enum Type : std::uint8_t { ACTIVATE_READ, DEFERRED_DELETE, CAPACITY_RELEASED } type_{};
  CommandHandler* destination_{nullptr};
  void* args_{nullptr};
};

} // namespace strij::event
