#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "test/e2e/support/environment.hh"
#include "test/e2e/support/process.hh"

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace strij::e2e {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

auto makeTempDir() -> std::filesystem::path {
  std::filesystem::path base = std::filesystem::temp_directory_path();
  if (const char* test_tmpdir = std::getenv("TEST_TMPDIR");
      test_tmpdir != nullptr && *test_tmpdir != '\0') {
    base = test_tmpdir;
  }

  std::string template_path = (base / "strij-process-test-XXXXXX").string();
  std::vector<char> buffer(template_path.begin(), template_path.end());
  buffer.push_back('\0');
  char* created = ::mkdtemp(buffer.data());
  return created == nullptr ? std::filesystem::path{} : std::filesystem::path{created};
}

TEST(ProcessTest, SpawnRedirectsOutputAndReportsExitStatus) {
  const auto directory = makeTempDir();
  ASSERT_FALSE(directory.empty());

  auto process = Spawn(SpawnOptions{
      .name = "echoer",
      .executable = "/bin/sh",
      .args = {"-c", "echo hi; echo to-stderr >&2"},
      .working_directory = directory.string(),
      .log_path = (directory / "echoer.log").string(),
  });
  ASSERT_TRUE(process.ok()) << process.status().message();

  ASSERT_TRUE(WaitForExit(*process, std::chrono::seconds{5}));
  auto status = ExitStatus(*process);
  ASSERT_TRUE(status.ok()) << status.status().message();
  EXPECT_EQ(*status, 0);

  const std::string log = LogTail((directory / "echoer.log").string(), 20);
  EXPECT_NE(log.find("hi"), std::string::npos) << log;
  EXPECT_NE(log.find("to-stderr"), std::string::npos) << log;
  EXPECT_EQ(DescendantsRemaining(*process), 0U);
}

TEST(ProcessTest, WaitForPortFindsASpawnedListenerAndTimesOutElsewhere) {
  const auto directory = makeTempDir();
  ASSERT_FALSE(directory.empty());

  PortAllocator ports;
  const uint16_t listening_port = ports.Allocate();
  ASSERT_NE(listening_port, 0);

  auto listener = Spawn(SpawnOptions{
      .name = "listener",
      .executable = RunfilePath("test/e2e/test_port_listener"),
      .args = {std::to_string(listening_port)},
      .working_directory = directory.string(),
      .log_path = (directory / "listener.log").string(),
  });
  ASSERT_TRUE(listener.ok()) << listener.status().message();

  auto listening = WaitForPort(listening_port, std::chrono::seconds{5}, &*listener);
  EXPECT_TRUE(listening.ok()) << listening.message();

  const uint16_t unused_port = ports.Allocate();
  ASSERT_NE(unused_port, 0);
  auto timeout = WaitForPort(unused_port, std::chrono::milliseconds{100});
  EXPECT_FALSE(timeout.ok());
  EXPECT_EQ(timeout.code(), absl::StatusCode::kDeadlineExceeded) << timeout.message();

  auto escalated = Terminate(*listener, std::chrono::seconds{2});
  ASSERT_TRUE(escalated.ok()) << escalated.status().message();
  EXPECT_FALSE(*escalated);
}

TEST(ProcessTest, TerminateEscalatesWhenTheSignalIsIgnored) {
  const auto directory = makeTempDir();
  ASSERT_FALSE(directory.empty());

  auto process = Spawn(SpawnOptions{
      .name = "wedged",
      .executable = "/bin/sh",
      .args = {"-c", "trap '' TERM; touch ready; while true; do sleep 0.05; done"},
      .working_directory = directory.string(),
      .log_path = (directory / "wedged.log").string(),
  });
  ASSERT_TRUE(process.ok()) << process.status().message();

  // Wait until the trap is installed; signalling before the shell reaches it
  // would kill the process by default and prove nothing.
  const auto ready_file = directory / "ready";
  const auto ready_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!std::filesystem::exists(ready_file) &&
         std::chrono::steady_clock::now() < ready_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  ASSERT_TRUE(std::filesystem::exists(ready_file));

  auto escalated = Terminate(*process, std::chrono::milliseconds{200});
  ASSERT_TRUE(escalated.ok()) << escalated.status().message();
  EXPECT_TRUE(*escalated);
  EXPECT_TRUE(HasExited(*process));
  EXPECT_EQ(DescendantsRemaining(*process), 0U);
}

TEST(ProcessTest, DescendantsRemainingSeesAndClearsShellChildren) {
  const auto directory = makeTempDir();
  ASSERT_FALSE(directory.empty());

  auto process = Spawn(SpawnOptions{
      .name = "parent",
      .executable = "/bin/sh",
      .args = {"-c", "sleep 60 & wait"},
      .working_directory = directory.string(),
      .log_path = (directory / "parent.log").string(),
  });
  ASSERT_TRUE(process.ok()) << process.status().message();

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
  while (DescendantsRemaining(*process) == 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  EXPECT_GT(DescendantsRemaining(*process), 0U);

  auto escalated = Terminate(*process, std::chrono::seconds{2});
  ASSERT_TRUE(escalated.ok()) << escalated.status().message();
  EXPECT_EQ(DescendantsRemaining(*process), 0U);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::e2e
