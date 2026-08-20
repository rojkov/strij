
#pragma once

namespace strij::event {

class CommandHandler;

struct Command {
  // DEFERRED_DELETE: the destination (a CommandHandler owning the object in
  // args_) destroys it outside the completion stack. Mirrors the
  // Connection::onEndOfStream teardown pattern.
  enum Type { ACTIVATE_READ, DEFERRED_DELETE } type_{};
  CommandHandler* destination_{nullptr};
  void* args_{nullptr};
};

} // namespace strij::event
