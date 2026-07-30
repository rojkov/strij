#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <netinet/in.h>

#include "carrot/event/command_handler.hh"
#include "carrot/event/completable.hh"
#include "carrot/event/dispatcher.hh"
#include "core/io/connection.hh"

namespace carrot::gateway {

class Node : public event::Completable, public event::CommandHandler {
public:
  enum class Status : uint8_t { kInitial, kConnecting, kConnected, kDisconnected };

  Node(std::string address, event::DispatcherSharedPtr dispatcher,
       carrot::io::ConnectionFactory factory);
  ~Node() override = default;

  Node(const Node&) = delete;
  auto operator=(const Node&) -> Node& = delete;
  Node(Node&&) noexcept = delete;
  auto operator=(Node&&) noexcept -> Node& = delete;

  void StartConnect();

  // Completable interface
  void HandleCompletion(uint8_t tag, int res, uint32_t flags) override;
  // CommandHandler interface
  void ProcessCommand(event::Command cmd) override;

  auto GetStatus() const -> Status { return status_; }
  auto GetConnection() -> carrot::io::Connection* { return connection_.get(); }
  auto GetAddress() const -> const std::string& { return address_; }
  auto IsAvailable() const -> bool { return status_ == Status::kConnected; }

private:
  enum Tags : uint8_t { kConnect = 0 };

  std::string address_;
  Status status_{Status::kInitial};
  int fd_{-1};
  struct sockaddr_in connect_addr_{};
  event::DispatcherSharedPtr dispatcher_;
  carrot::io::ConnectionFactory factory_;
  std::unique_ptr<carrot::io::Connection> connection_;
};

} // namespace carrot::gateway
