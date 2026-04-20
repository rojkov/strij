
#pragma once

namespace carrot {
namespace event {

class IOObject;

struct Command {
  enum type_ { ACTIVATE_READ };
  IOObject* destination_{nullptr};
  void* args_{nullptr};
};

} // namespace event
} // namespace carrot
