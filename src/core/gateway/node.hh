#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <netinet/in.h>

#include "strij/event/command_handler.hh"
#include "strij/event/completable.hh"
#include "strij/event/dispatcher.hh"
#include "core/io/connection.hh"
#include "core/node/capabilities.pb.h"

namespace strij::gateway {

class Node : public event::Completable, public event::CommandHandler {
public:
  enum class Status : uint8_t { kInitial, kConnecting, kConnected, kDisconnected };

  Node(std::string node_id, std::string address, event::DispatcherSharedPtr dispatcher,
       strij::io::ConnectionFactory factory);
  ~Node() override = default;

  Node(const Node&) = delete;
  auto operator=(const Node&) -> Node& = delete;
  Node(Node&&) noexcept = delete;
  auto operator=(Node&&) noexcept -> Node& = delete;

  void StartConnect();
  // Closes the node's connection (if any) and marks it disconnected.
  void Disconnect();
  // Closes the current connection, switches to a new address, and reconnects.
  void ReconnectTo(std::string address);

  // Completable interface
  void HandleCompletion(uint8_t tag, int res, uint32_t flags) override;
  // CommandHandler interface
  void ProcessCommand(event::Command cmd) override;

  auto GetStatus() const -> Status { return status_; }
  auto GetConnection() -> strij::io::Connection* { return connection_.get(); }
  auto GetNodeId() const -> const std::string& { return node_id_; }
  auto GetAddress() const -> const std::string& { return address_; }
  auto IsAvailable() const -> bool { return status_ == Status::kConnected; }

  // Updates the node's identity to the one advertised by the nodeagent.
  void SetNodeId(std::string node_id);

  // Stores the capabilities received in the node's kNodeAdvertisement frame.
  void StoreCapabilities(strij::node::NodeCapabilities capabilities);
  // Returns the advertised capabilities, or nullptr if none received yet.
  auto GetCapabilities() const -> const strij::node::NodeCapabilities*;
  // Stores the latest kNodeState snapshot received from the nodeagent.
  void UpdateState(strij::node::NodeState state);
  // Returns the latest state snapshot, or nullptr if none received yet.
  auto GetState() const -> const strij::node::NodeState*;

private:
  enum Tags : uint8_t { kConnect = 0 };

  std::string node_id_;
  std::string address_;
  Status status_{Status::kInitial};
  int fd_{-1};
  struct sockaddr_in connect_addr_{};
  event::DispatcherSharedPtr dispatcher_;
  strij::io::ConnectionFactory factory_;
  std::unique_ptr<strij::io::Connection> connection_;
  std::unique_ptr<strij::node::NodeCapabilities> capabilities_;
  std::unique_ptr<strij::node::NodeState> state_;
};

} // namespace strij::gateway
