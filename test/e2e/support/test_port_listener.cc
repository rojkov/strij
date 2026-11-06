#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Binds and listens on the port given as argv[1], then blocks until signalled.
// Used by process_test as a spawned process whose listener WaitForPort can find.
int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <port>\n", argv[0]);
    return 2;
  }

  const int port = std::atoi(argv[1]);
  if (port <= 0 || port > 65535) {
    std::fprintf(stderr, "invalid port: %s\n", argv[1]);
    return 2;
  }

  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    std::perror("socket");
    return 1;
  }

  int reuse = 1;
  (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<uint16_t>(port));

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    std::perror("bind");
    return 1;
  }
  if (::listen(fd, 16) != 0) {
    std::perror("listen");
    return 1;
  }

  std::printf("listening on %d\n", port);
  std::fflush(stdout);
  for (;;) {
    ::pause();
  }
}
