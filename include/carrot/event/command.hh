
#pragma once

namespace carrot::event {

class IOObject;

struct Command {
  enum Type { ACTIVATE_READ } type_{};
  IOObject* destination_{nullptr};
  void* args_{nullptr};
};

} // namespace carrot::event
