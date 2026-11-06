#include "test/e2e/support/environment.hh"

#include <cstdlib>
#include <filesystem>

#include "gtest/gtest.h"
#include "liburing.h"

namespace strij::e2e {

auto RunfilePath(std::string_view relative_path) -> std::string {
  const auto append_relative =
      [relative_path](const std::filesystem::path& base) -> std::filesystem::path {
    if (relative_path.empty()) {
      return base;
    }
    return base / std::filesystem::path(relative_path);
  };

  if (const char* test_srcdir = std::getenv("TEST_SRCDIR"); test_srcdir != nullptr) {
    std::filesystem::path base{test_srcdir};
    if (const char* test_workspace = std::getenv("TEST_WORKSPACE");
        test_workspace != nullptr && *test_workspace != '\0') {
      base /= test_workspace;
    }
    return append_relative(base).string();
  }

  if (const char* runfiles_dir = std::getenv("RUNFILES_DIR"); runfiles_dir != nullptr) {
    std::filesystem::path base{runfiles_dir};
    if (const char* test_workspace = std::getenv("TEST_WORKSPACE");
        test_workspace != nullptr && *test_workspace != '\0') {
      base /= test_workspace;
    }
    return append_relative(base).string();
  }

  return std::string(relative_path);
}

void RequireIoUring() {
  io_uring ring{};
  const int result = io_uring_queue_init(8, &ring, 0);
  if (result < 0) {
    GTEST_SKIP() << "io_uring is unavailable (io_uring_queue_init failed with errno " << -result
                 << "); the gateway and nodeagent cannot start here. Run this suite on a host "
                    "that permits io_uring_setup, or use the manual procedure in "
                    "docs/testing-openworkflow.md.";
    return;
  }
  io_uring_queue_exit(&ring);
}

} // namespace strij::e2e
