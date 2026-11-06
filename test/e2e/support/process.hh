#pragma once

#include <sys/types.h>

#include <chrono>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace strij::e2e {

/// A process started by the harness. `exited`/`raw_wait_status` are populated on
/// the first successful non-blocking waitpid; `escalated` records that teardown
/// had to send SIGKILL after the graceful signal timed out.
struct Process {
  std::string name;
  pid_t pid{-1};
  pid_t pgid{-1};
  std::string log_path;
  bool exited{false};
  bool escalated{false};
  int raw_wait_status{0};
};

struct SpawnOptions {
  std::string name;
  std::string executable;
  /// Arguments after argv[0].
  std::vector<std::string> args;
  std::string working_directory;
  std::string log_path;
};

/// Hands out loopback ports from the operating system (`bind(0)`), remembering
/// what it already handed out so two processes in one cluster cannot be given
/// the same closed-over port.
class PortAllocator {
public:
  PortAllocator() = default;
  auto Allocate() -> uint16_t;

private:
  std::set<uint16_t> allocated_;
};

/// Starts `options.executable` as a process-group leader in its own working
/// directory, with stdout and stderr redirected to `options.log_path`.
auto Spawn(const SpawnOptions& options) -> absl::StatusOr<Process>;

/// Polls the process without blocking and records its exit. Returns true once it
/// has exited. Safe to call repeatedly; later calls return the recorded result.
auto HasExited(Process& process) -> bool;

/// Waits up to `timeout` for the process to exit. Returns true if it exited.
auto WaitForExit(Process& process, std::chrono::milliseconds timeout) -> bool;

/// Returns the process exit code. Fails when the process has not exited or was
/// terminated by a signal (escalation is reported through `Terminate`).
auto ExitStatus(Process& process) -> absl::StatusOr<int>;

/// Human-readable process state ("exit status 0", "running", ...).
auto DescribeExit(Process& process) -> std::string;

/// Polls TCP connect against 127.0.0.1:`port` until it accepts or `timeout`
/// expires. When `process` is non-null, an observed exit fails fast with the
/// process name, exit status, and log tail instead of waiting out the deadline.
auto WaitForPort(uint16_t port, std::chrono::milliseconds timeout, Process* process = nullptr)
    -> absl::Status;

/// Performs one non-blocking TCP connect. Returns true when the port accepts.
auto IsPortAccepting(uint16_t port) -> bool;

/// Returns the last `line_count` lines of a log file (empty when unreadable).
auto LogTail(const std::string& path, std::size_t line_count) -> std::string;

/// Sends SIGTERM to the process group, waits up to `grace`, then SIGKILLs the
/// group when the process is still alive. Reaps the leader. Returns whether
/// escalation was needed.
auto Terminate(Process& process, std::chrono::milliseconds grace) -> absl::StatusOr<bool>;

/// Counts live (non-zombie) processes other than the leader whose process group
/// is the leader's group. Descendants inherit the group unless they call
/// setsid(2), so this observes children a spawned shell leaves behind.
auto DescendantsRemaining(const Process& process) -> std::size_t;

} // namespace strij::e2e
