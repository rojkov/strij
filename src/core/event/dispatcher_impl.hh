#pragma once

#include <vector>

#include "carrot/event/dispatcher.hh"

namespace carrot::event {

class DispatcherImpl : public Dispatcher {
public:
  DispatcherImpl();
  [[nodiscard]] auto GetRing() -> struct io_uring* override;
  void Run() override;
  void Shutdown() override;
  void SubmitCommand(Command cmd) override;

private:
  const uint32_t entries_num_{4096};
  struct io_uring ring_{};
  std::vector<Command> command_queue_;
  bool is_finishing_{false};
};

} // namespace carrot::event
