#pragma once

#include "carrot/event/dispatcher.hh"
#include "carrot/event/io_object.hh"
#include "rigtorp/SPSCQueue.h"

namespace carrot::logging {

class LogFrontend : public event::IOObject {
public:
  explicit LogFrontend(event::DispatcherSharedPtr dispatcher);

  void Log(const std::string& msg);

  // event::IOObject interface
  void HandleCompletion(int res, uint32_t flags) override;
  void ProcessCommand(event::Command cmd) override;

private:
  static constexpr std::size_t queue_size = 1024;
  rigtorp::SPSCQueue<std::string> queue_{queue_size};
  int event_fd_;
  uint64_t event_fd_val_;
  event::DispatcherSharedPtr dispatcher_;
};

} // namespace carrot::logging