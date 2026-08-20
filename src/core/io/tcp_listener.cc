#include "core/io/tcp_listener.hh"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <format>
#include <memory>
#include <stdexcept>

#include "core/logging/log.hh"
#include "core/utils/errors.hh"
#include "liburing.h"

namespace strij::io {

TcpListener::TcpListener(event::DispatcherSharedPtr dispatcher, uint32_t port,
                         ConnectionFactory factory)
    : dispatcher_{std::move(dispatcher)},
      listen_fd_{socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)}, factory_{std::move(factory)} {
  if (listen_fd_ < 0) {
    throw std::runtime_error("unable to open a TCP socket");
  }

  int opt_value{1};
  errno = 0;
  if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &opt_value, sizeof(opt_value)) < 0) {
    throw std::runtime_error(
        std::format("unable to socket options: {}", utils::GetErrorString(errno)));
  }

  struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(port)};
  errno = 0;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    throw std::runtime_error(
        std::format("unable to bind to address: {}", utils::GetErrorString(errno)));
  }

  errno = 0;
  if (listen(listen_fd_, SOMAXCONN) < 0) {
    throw std::runtime_error(std::format("unable to listen: {}", utils::GetErrorString(errno)));
  }

  dispatcher_->PrepareAcceptMultishot(this, 0, listen_fd_);
}

void TcpListener::HandleCompletion(uint8_t /*tag*/, int res, uint32_t flags) {
  if (res < 0) {
    LOG_ERROR("unable to accept connection");
    return;
  }

  LOG_DEBUG("got new connection. res: {}, flags: {}", res, flags);

  if ((flags & IORING_CQE_F_MORE) == 0) {
    // If this flag is missing, the multi-shot was cancelled or failed.
    // TODO: perhaps check for shutdown being in progress.
    LOG_WARNING("no more multishot accepts. Were they canceled?");
  }

  owned_connections_.push_back(std::make_unique<Connection>(res, dispatcher_, this, factory_));
}

void TcpListener::ProcessCommand(event::Command cmd) {
  if (cmd.type_ == event::Command::DEFERRED_DELETE) {
    auto* conn = static_cast<Connection*>(cmd.args_);
    auto iter = std::find_if(owned_connections_.begin(), owned_connections_.end(),
                             [conn](const auto& ptr) -> bool { return ptr.get() == conn; });
    if (iter != owned_connections_.end()) {
      owned_connections_.erase(iter);
    }
  }
}

} // namespace strij::io
