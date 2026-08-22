#include "core/gateway/node.hh"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstdint>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "core/gateway/result_receiver_storage.hh"
#include "core/logging/log.hh"
#include "core/utils/errors.hh"

namespace strij::gateway {

Node::Node(std::string node_id, std::string address, event::DispatcherSharedPtr dispatcher,
           io::ConnectionFactory factory, ResultReceiverStorage& storage)
    : node_id_{std::move(node_id)}, address_{std::move(address)},
      dispatcher_{std::move(dispatcher)}, factory_{std::move(factory)}, storage_{storage} {}

void Node::SetNodeId(std::string node_id) { node_id_ = std::move(node_id); }

void Node::StoreCapabilities(node::NodeCapabilities&& capabilities) {
  capabilities_ = std::make_unique<node::NodeCapabilities>(std::move(capabilities));
}

auto Node::GetCapabilities() const -> const node::NodeCapabilities* { return capabilities_.get(); }

void Node::UpdateState(node::NodeState&& state) {
  state_ = std::make_unique<node::NodeState>(std::move(state));
}

auto Node::GetState() const -> const node::NodeState* { return state_.get(); }

void Node::Disconnect() {
  if (connection_ != nullptr) {
    connection_->Close();
    connection_.reset();
  }
  status_ = Status::kDisconnected;
}

void Node::ReconnectTo(std::string address) {
  Disconnect();
  address_ = std::move(address);
  status_ = Status::kInitial;
  StartConnect();
}

void Node::StartConnect() {
  assert(status_ == Status::kInitial);

  auto colon_pos = address_.find(':');
  if (colon_pos == std::string::npos) {
    throw std::runtime_error(std::format("invalid address format: {}", address_));
  }
  auto host = address_.substr(0, colon_pos);
  auto port = static_cast<uint16_t>(std::stoul(address_.substr(colon_pos + 1)));

  fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (fd_ < 0) {
    throw std::runtime_error("failed to create socket");
  }

  connect_addr_ = {};
  connect_addr_.sin_family = AF_INET;
  connect_addr_.sin_port = htons(port);

  if (inet_pton(AF_INET, host.c_str(), &connect_addr_.sin_addr) != 1) {
    ::close(fd_);
    fd_ = -1;
    throw std::runtime_error(std::format("invalid address: {}", host));
  }

  dispatcher_->PrepareConnect(this, kConnect, fd_,
                              // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                              reinterpret_cast<struct sockaddr*>(&connect_addr_),
                              sizeof(connect_addr_));
  status_ = Status::kConnecting;

  LOG_INFO("Connecting to {}:{}", host, port);
}

void Node::HandleCompletion(uint8_t tag, int res, uint32_t /*flags*/) {
  if (tag == kConnect) {
    if (res == 0) {
      connection_ = std::make_unique<io::Connection>(fd_, dispatcher_, this, std::move(factory_));
      fd_ = -1;
      status_ = Status::kConnected;

      // Clean up orphaned receivers if this node connection drops.
      connection_->Mailbox()->RegisterOnClose([this]() {
        storage_.NotifyNodeDisconnected(node_id_);
      });

      LOG_INFO("Connected to {}", address_);
    } else {
      LOG_WARNING("Failed to connect to {}: {}", address_, utils::GetErrorString(-res));
      ::close(fd_);
      fd_ = -1;
      status_ = Status::kDisconnected;
    }
  }
}

void Node::ProcessCommand(event::Command cmd) {
  if (cmd.type_ == event::Command::DEFERRED_DELETE) {
    connection_.reset();
    status_ = Status::kDisconnected;
  }
}

} // namespace strij::gateway
