#include "core/io/tcp_connector.hh"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <format>
#include <stdexcept>

#include "core/logging/log.hh"

namespace carrot::io {

TcpConnector::TcpConnector(event::DispatcherSharedPtr dispatcher, ResultReceiverStorage& storage)
    : dispatcher_{std::move(dispatcher)}, storage_{storage} {}

Connection* TcpConnector::Connect(const std::string& host, uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    throw std::runtime_error("failed to create socket");
  }

  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    throw std::runtime_error(std::format("invalid address: {}", host));
  }

  if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    throw std::runtime_error(std::format("failed to connect to {}:{}", host, port));
  }

  LOG_INFO("Connected to {}:{}", host, port);

  auto factory = [this](carrot::io::Connection& conn) -> std::unique_ptr<ProtocolParser> {
    auto handler = std::make_unique<GatewayTlvHandler>(storage_);
    return std::make_unique<TlvParser>(
        [h = std::move(handler), &conn](TlvFrame frame) { h->HandleFrame(frame, conn); });
  };

  connections_.push_back(
      std::make_unique<Connection>(fd, dispatcher_, this, std::move(factory)));

  return connections_.back().get();
}

void TcpConnector::HandleCompletion(uint8_t /*tag*/, int /*res*/, uint32_t /*flags*/) {}

void TcpConnector::ProcessCommand(event::Command cmd) {
  if (cmd.type_ == event::Command::CLOSE_CONNECTION) {
    auto* conn = static_cast<Connection*>(cmd.args_);
    auto it = std::find_if(connections_.begin(), connections_.end(),
                           [conn](const auto& ptr) -> bool { return ptr.get() == conn; });
    if (it != connections_.end()) {
      connections_.erase(it);
    }
  }
}

} // namespace carrot::io
