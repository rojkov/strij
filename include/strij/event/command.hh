
#pragma once

namespace strij::event {

class CommandHandler;

struct Command {
  enum Type { ACTIVATE_READ, CLOSE_CONNECTION } type_{};
  CommandHandler* destination_{nullptr};
  void* args_{nullptr};
};

} // namespace strij::event
