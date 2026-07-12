#include "core/io/tcp_listener.hh"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <memory>

#include "core/io/http_echo_handler.hh"
#include "core/io/llhttp_parser.hh"
#include "core/logging/log.hh"
#include "liburing.h"

namespace carrot::io {

namespace {

auto makeDefaultFactory() -> ConnectionFactory {
  return [](std::function<void(std::span<const std::byte>)> on_message)
             -> std::pair<ProtocolParserPtr, MessageHandlerPtr> {
    return std::make_pair<ProtocolParserPtr, MessageHandlerPtr>(
        std::make_unique<LlhttpParser>(std::move(on_message)), std::make_unique<HttpEchoHandler>());
  };
}

} // namespace

TcpListener::TcpListener(event::DispatcherSharedPtr dispatcher, uint32_t port,
                         ConnectionFactory factory)
    : dispatcher_{std::move(dispatcher)},
      listen_fd_{socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)},
      factory_{factory ? std::move(factory) : makeDefaultFactory()} {
  if (listen_fd_ < 0) {
    throw std::runtime_error("unable to open a TCP socket");
  }

  int opt_value{1};
  errno = 0;
  if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &opt_value, sizeof(opt_value)) < 0) {
    throw std::runtime_error(std::format("unable to socket options: {}", std::strerror(errno)));
  }

  struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(port)};
  errno = 0;
  if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
    throw std::runtime_error(std::format("unable to bind to address: {}", std::strerror(errno)));
  }

  errno = 0;
  if (listen(listen_fd_, SOMAXCONN) < 0) {
    throw std::runtime_error(std::format("unable to listen: {}", std::strerror(errno)));
  }

  dispatcher_->PrepareAcceptMultishot(this, 0, listen_fd_);
}

void TcpListener::HandleCompletion(uint8_t tag, int res, uint32_t flags) {
  if (res < 0) {
    LOG_ERROR("unable to accept connection");
    return;
  }

  LOG_DEBUG("got new connection. res: {}, flags: {}", res, flags);

  if (!(flags & IORING_CQE_F_MORE)) {
    // If this flag is missing, the multi-shot was cancelled or failed.
    // TODO: perhaps check for shutdown being in progress.
    LOG_WARNING("no more multishot accepts. Were they canceled?");
  }

  owned_connections_.push_back(std::make_unique<Connection>(res, dispatcher_, this, factory_));
}

void TcpListener::ProcessCommand(event::Command cmd) {
  if (cmd.type_ == event::Command::CLOSE_CONNECTION) {
    auto* conn = static_cast<Connection*>(cmd.args_);
    auto it = std::find_if(owned_connections_.begin(), owned_connections_.end(),
                           [conn](const auto& ptr) -> bool { return ptr.get() == conn; });
    if (it != owned_connections_.end()) {
      owned_connections_.erase(it);
    }
  }
}

} // namespace carrot::io
