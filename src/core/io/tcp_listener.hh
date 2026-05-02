#pragma once

#include "carrot/event/dispatcher.hh"
#include "carrot/event/io_object.hh"

namespace carrot::io {

class TcpListener : public event::IOObject {
public:
  TcpListener(event::DispatcherSharedPtr dispatcher, uint32_t port);

  // IOObject interface
  void HandleCompletion(int res, uint32_t flags) override;
  void ProcessCommand(event::Command cmd) override;

private:
  event::DispatcherSharedPtr dispatcher_;
  int listen_fd_;
};

} // namespace carrot::io
