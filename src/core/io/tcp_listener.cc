#include "core/io/tcp_listener.hh"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstring>
#include <format>

#include "core/logging/log.hh"
#include "liburing.h"

namespace carrot::io {

TcpListener::TcpListener(event::DispatcherSharedPtr dispatcher, uint32_t port)
    : dispatcher_{std::move(dispatcher)},
      listen_fd_{socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)} {
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

  dispatcher_->PrepareAcceptMultishot(this, listen_fd_);
}

void TcpListener::HandleCompletion(int res, uint32_t flags) {
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

  owned_connections_.emplace_back(new Connection(res, dispatcher_));
}

void TcpListener::ProcessCommand(event::Command cmd) {}

} // namespace carrot::io
