#include <cstdio>
#include <cstring>

#include "liburing.h"

int main() {
  io_uring ring{};
  const int result = io_uring_queue_init(8, &ring, 0);
  if (result < 0) {
    std::fprintf(stderr, "io_uring_queue_init failed: %s (%d)\n", std::strerror(-result), result);
    return 1;
  }

  io_uring_queue_exit(&ring);
  std::puts("io_uring_queue_init succeeded");
  return 0;
}
