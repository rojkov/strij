#pragma once

#include <memory>
#include <vector>

#include "strij/event/command_handler.hh"
#include "strij/event/completable.hh"
#include "strij/event/dispatcher.hh"
#include "core/io/connection.hh"

namespace strij::io {

class TcpListener : public event::Completable, public event::CommandHandler {
public:
  TcpListener(event::DispatcherSharedPtr dispatcher, uint32_t port, ConnectionFactory factory);

  // Completable interface
  void HandleCompletion(uint8_t tag, int res, uint32_t flags) override;
  // CommandHandler interface
  void ProcessCommand(event::Command cmd) override;

private:
  event::DispatcherSharedPtr dispatcher_;
  int listen_fd_;
  ConnectionFactory factory_;
  std::vector<std::unique_ptr<Connection>> owned_connections_;
};

} // namespace strij::io
