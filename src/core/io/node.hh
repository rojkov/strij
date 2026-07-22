#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <netinet/in.h>

#include "carrot/event/dispatcher.hh"
#include "carrot/event/io_object.hh"
#include "core/io/connection.hh"

namespace carrot::io {

// TODO: Add reconnection logic for disconnected nodes.
class Node : public event::IOObject {
public:
  enum class Status : uint8_t { kInitial, kConnecting, kConnected, kDisconnected };

  Node(std::string address, event::DispatcherSharedPtr dispatcher, ConnectionFactory factory);
  ~Node() override = default;

  Node(const Node&) = delete;
  auto operator=(const Node&) -> Node& = delete;
  Node(Node&&) noexcept = delete;
  auto operator=(Node&&) noexcept -> Node& = delete;

  void StartConnect();

  // IOObject interface
  void HandleCompletion(uint8_t tag, int res, uint32_t flags) override;
  void ProcessCommand(event::Command cmd) override;

  auto GetStatus() const -> Status { return status_; }
  auto GetConnection() -> Connection* { return connection_.get(); }
  auto GetAddress() const -> const std::string& { return address_; }
  auto IsAvailable() const -> bool { return status_ == Status::kConnected; }

private:
  enum Tags : uint8_t { kConnect = 0 };

  std::string address_;
  Status status_{Status::kInitial};
  int fd_{-1};
  struct sockaddr_in connect_addr_{};
  event::DispatcherSharedPtr dispatcher_;
  ConnectionFactory factory_;
  std::unique_ptr<Connection> connection_;
};

} // namespace carrot::io
