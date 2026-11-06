#include "test/e2e/support/process.hh"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

extern char** environ;

namespace strij::e2e {

namespace {

constexpr auto kPollInterval = std::chrono::milliseconds{2};

auto errnoMessage(int error) -> std::string { return std::strerror(error); }

auto joinArgs(const std::vector<std::string>& args) -> std::string {
  std::string joined;
  for (const auto& arg : args) {
    if (!joined.empty()) {
      joined.push_back(' ');
    }
    joined += arg;
  }
  return joined;
}

} // namespace

auto PortAllocator::Allocate() -> uint16_t {
  constexpr int kMaxAttempts = 100;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      return 0;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
      ::close(fd);
      return 0;
    }

    socklen_t length = sizeof(address);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
      ::close(fd);
      return 0;
    }
    ::close(fd);

    const auto port = ntohs(address.sin_port);
    if (port == 0 || allocated_.contains(port)) {
      continue;
    }
    allocated_.insert(port);
    return port;
  }
  return 0;
}

auto Spawn(const SpawnOptions& options) -> absl::StatusOr<Process> {
  if (options.executable.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("process '", options.name, "' has no executable"));
  }

  if (!options.working_directory.empty()) {
    std::error_code error;
    std::filesystem::create_directories(options.working_directory, error);
    if (error) {
      return absl::InternalError(absl::StrCat("cannot create working directory '",
                                              options.working_directory, "' for process '",
                                              options.name, "': ", error.message()));
    }
  }

  if (!options.log_path.empty()) {
    std::error_code error;
    const auto parent = std::filesystem::path(options.log_path).parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent, error);
      if (error) {
        return absl::InternalError(absl::StrCat("cannot create log directory '", parent.string(),
                                                "' for process '", options.name,
                                                "': ", error.message()));
      }
    }
  }

  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    return absl::InternalError(
        absl::StrCat("posix_spawn_file_actions_init failed for process '", options.name, "'"));
  }

  // stdin is /dev/null so a child that reads it never blocks the harness. stdout
  // gets the log file and stderr duplicates it, so both streams share one file
  // offset (D8).
  (void)posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  (void)posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, options.log_path.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
  (void)posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
  if (!options.working_directory.empty()) {
#if defined(__GLIBC__)
    (void)posix_spawn_file_actions_addchdir_np(&actions, options.working_directory.c_str());
#endif
  }

  posix_spawnattr_t attributes;
  if (posix_spawnattr_init(&attributes) != 0) {
    posix_spawn_file_actions_destroy(&actions);
    return absl::InternalError(
        absl::StrCat("posix_spawnattr_init failed for process '", options.name, "'"));
  }
  // A process-group leader is what makes group-wide teardown catch descendants
  // a task spawned (D7).
  (void)posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
  (void)posix_spawnattr_setpgroup(&attributes, 0);

  std::vector<std::string> argv_storage;
  argv_storage.reserve(options.args.size() + 1);
  argv_storage.push_back(options.executable);
  argv_storage.insert(argv_storage.end(), options.args.begin(), options.args.end());

  std::vector<char*> argv;
  argv.reserve(argv_storage.size() + 1);
  for (auto& arg : argv_storage) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

  pid_t pid = -1;
  const int result =
      ::posix_spawn(&pid, options.executable.c_str(), &actions, &attributes, argv.data(), environ);

  posix_spawnattr_destroy(&attributes);
  posix_spawn_file_actions_destroy(&actions);

  if (result != 0) {
    return absl::InternalError(absl::StrCat(
        "failed to spawn process '", options.name, "' ('", options.executable,
        (!options.args.empty() ? absl::StrCat(" ", joinArgs(options.args)) : std::string{}),
        "'): ", errnoMessage(result)));
  }

  Process process;
  process.name = options.name;
  process.pid = pid;
  process.pgid = pid;
  process.log_path = options.log_path;
  return process;
}

auto HasExited(Process& process) -> bool {
  if (process.exited) {
    return true;
  }
  if (process.pid <= 0) {
    process.exited = true;
    return true;
  }

  int status = 0;
  const pid_t reaped = ::waitpid(process.pid, &status, WNOHANG);
  if (reaped == process.pid) {
    process.exited = true;
    process.raw_wait_status = status;
    return true;
  }
  if (reaped < 0 && errno == ECHILD) {
    // Someone else reaped it (e.g. a previous call on a different object).
    process.exited = true;
    process.pid = -1;
    return true;
  }
  return false;
}

auto WaitForExit(Process& process, std::chrono::milliseconds timeout) -> bool {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    if (HasExited(process)) {
      return true;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(kPollInterval);
  }
}

auto ExitStatus(Process& process) -> absl::StatusOr<int> {
  if (!HasExited(process)) {
    return absl::FailedPreconditionError(
        absl::StrCat("process '", process.name, "' is still running"));
  }
  if (process.raw_wait_status == 0) {
    return 0;
  }
  if (WIFEXITED(process.raw_wait_status)) {
    return WEXITSTATUS(process.raw_wait_status);
  }
  if (WIFSIGNALED(process.raw_wait_status)) {
    return absl::AbortedError(absl::StrCat("process '", process.name, "' was terminated by signal ",
                                           WTERMSIG(process.raw_wait_status)));
  }
  return absl::InternalError(
      absl::StrCat("process '", process.name, "' has an unreadable wait status"));
}

auto DescribeExit(Process& process) -> std::string {
  if (!process.exited) {
    return "running";
  }
  if (WIFEXITED(process.raw_wait_status)) {
    return absl::StrCat("exit status ", WEXITSTATUS(process.raw_wait_status));
  }
  if (WIFSIGNALED(process.raw_wait_status)) {
    return absl::StrCat("terminated by signal ", WTERMSIG(process.raw_wait_status));
  }
  return "exited with an unreadable status";
}

auto IsPortAccepting(uint16_t port) -> bool {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const int result = ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  ::close(fd);
  return result == 0;
}

auto WaitForPort(uint16_t port, std::chrono::milliseconds timeout, Process* process)
    -> absl::Status {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    if (IsPortAccepting(port)) {
      return absl::OkStatus();
    }

    if (process != nullptr && HasExited(*process)) {
      return absl::UnavailableError(absl::StrCat(
          "process '", process->name, "' exited before its listener on port ", port, " accepted (",
          DescribeExit(*process), "); log tail:\n", LogTail(process->log_path, 40)));
    }

    if (std::chrono::steady_clock::now() >= deadline) {
      return absl::DeadlineExceededError(absl::StrCat("timed out after ", timeout.count(),
                                                      " ms waiting for port ", port,
                                                      " to accept a connection"));
    }
    std::this_thread::sleep_for(kPollInterval);
  }
}

auto LogTail(const std::string& path, std::size_t line_count) -> std::string {
  if (path.empty()) {
    return {};
  }
  std::ifstream input(path);
  if (!input) {
    return {};
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
    if (lines.size() > line_count * 4) {
      lines.erase(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(line_count * 2));
    }
  }

  const std::size_t begin = lines.size() > line_count ? lines.size() - line_count : 0;
  std::string tail;
  for (std::size_t index = begin; index < lines.size(); ++index) {
    tail += lines[index];
    tail.push_back('\n');
  }
  return tail;
}

auto Terminate(Process& process, std::chrono::milliseconds grace) -> absl::StatusOr<bool> {
  if (process.pgid > 0) {
    // Send to the group even when the leader already exited: a task can leave
    // grandchildren behind in the same group after the leader is reaped.
    if (::killpg(process.pgid, SIGTERM) != 0 && errno != ESRCH) {
      return absl::InternalError(absl::StrCat("failed to signal process group ", process.pgid,
                                              " for process '", process.name,
                                              "': ", errnoMessage(errno)));
    }
  } else if (process.pid > 0) {
    ::kill(process.pid, SIGTERM);
  }

  const auto deadline = std::chrono::steady_clock::now() + grace;
  for (;;) {
    const bool leader_done = HasExited(process);
    if (leader_done && DescendantsRemaining(process) == 0) {
      return false;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      break;
    }
    std::this_thread::sleep_for(kPollInterval);
  }

  process.escalated = true;
  if (process.pgid > 0) {
    if (::killpg(process.pgid, SIGKILL) != 0 && errno != ESRCH) {
      return absl::InternalError(absl::StrCat("failed to force-kill process group ", process.pgid,
                                              " for process '", process.name,
                                              "': ", errnoMessage(errno)));
    }
  } else if (process.pid > 0) {
    ::kill(process.pid, SIGKILL);
  }

  constexpr auto kKillWait = std::chrono::seconds{5};
  const auto kill_deadline = std::chrono::steady_clock::now() + kKillWait;
  for (;;) {
    const bool leader_done = HasExited(process);
    if (leader_done && DescendantsRemaining(process) == 0) {
      return true;
    }
    if (std::chrono::steady_clock::now() >= kill_deadline) {
      return absl::InternalError(absl::StrCat("process '", process.name,
                                              "' or its descendants did not exit after SIGKILL"));
    }
    std::this_thread::sleep_for(kPollInterval);
  }
}

auto DescendantsRemaining(const Process& process) -> std::size_t {
  if (process.pgid <= 0) {
    return 0;
  }

  std::size_t count = 0;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator("/proc", error)) {
    if (error) {
      break;
    }
    const std::string name = entry.path().filename().string();
    if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos) {
      continue;
    }

    std::ifstream stat_file(entry.path() / "stat");
    std::string stat_line;
    if (!stat_file || !std::getline(stat_file, stat_line)) {
      continue;
    }

    // Field layout: pid (comm) state ppid pgrp ... ; comm may contain spaces and
    // parentheses, so split only after the final ')'.
    const auto close_paren = stat_line.rfind(')');
    if (close_paren == std::string::npos || close_paren + 2 >= stat_line.size()) {
      continue;
    }
    std::string_view rest(stat_line.data() + close_paren + 1, stat_line.size() - close_paren - 1);

    // rest is " state ppid pgrp ...".
    std::vector<std::string_view> fields;
    std::size_t position = 0;
    while (position < rest.size() && fields.size() < 3) {
      while (position < rest.size() && rest[position] == ' ') {
        ++position;
      }
      const std::size_t start = position;
      while (position < rest.size() && rest[position] != ' ') {
        ++position;
      }
      if (position > start) {
        fields.push_back(rest.substr(start, position - start));
      }
    }
    if (fields.size() < 3) {
      continue;
    }

    const std::string_view state = fields[0];
    if (state == "Z" || state == "X" || state == "x") {
      continue;
    }

    const long long pid = std::stoll(name);
    long long pgrp = 0;
    try {
      pgrp = std::stoll(std::string(fields[2]));
    } catch (const std::exception&) {
      continue;
    }

    if (pgrp == static_cast<long long>(process.pgid) &&
        pid != static_cast<long long>(process.pid)) {
      ++count;
    }
  }
  return count;
}

} // namespace strij::e2e
