#pragma once

#include <vector>

#include "carrot/event/dispatcher.hh"
#include "carrot/event/io_object.hh"
#include "core/io/connection.hh"

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
  std::vector<std::unique_ptr<Connection>> owned_connections_;
};

} // namespace carrot::io
