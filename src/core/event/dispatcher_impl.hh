#pragma once

#include "carrot/event/dispatcher.hh"
#include "liburing.h"

namespace carrot {
namespace event {

class DispatcherImpl : public Dispatcher {
public:
  DispatcherImpl();
  void Run() override;

private:
  const uint32_t entries_num_{4096};
  struct io_uring ring_{};
  ;
};

} // namespace event
} // namespace carrot
