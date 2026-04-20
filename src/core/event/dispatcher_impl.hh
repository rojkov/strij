#pragma once

#include <vector>

#include "carrot/event/dispatcher.hh"
#include "liburing.h"

namespace carrot {
namespace event {

class DispatcherImpl : public Dispatcher {
public:
  DispatcherImpl();
  void Run() override;
  void SubmitCommand(Command cmd) override;

private:
  const uint32_t entries_num_{4096};
  struct io_uring ring_{};
  std::vector<Command> command_queue_;
  ;
};

} // namespace event
} // namespace carrot
