#include "nodeagent/extensions/task_handlers/pyfunction/worker_process.hh"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "common/core/logging/log.hh"

extern char** environ;

namespace strij::nodeagent::task_handlers::pyfunction {
namespace {

constexpr int kInvalidFd = -1;

void closeFd(int* fd) {
  if (*fd != kInvalidFd) {
    ::close(*fd);
    *fd = kInvalidFd;
  }
}

void upsertEnvironment(std::vector<std::string>& environment, std::string_view key,
                       std::string_view value) {
  const std::string prefix = std::string(key) + "=";
  environment.erase(
      std::remove_if(environment.begin(), environment.end(),
                     [&prefix](const std::string& entry) { return entry.starts_with(prefix); }),
      environment.end());
  environment.push_back(prefix + std::string(value));
}

} // namespace

WorkerProcess::WorkerProcess(event::Dispatcher& dispatcher, WorkerSpawnConfig config,
                             WorkerCallbacks callbacks)
    : dispatcher_{&dispatcher}, config_{std::move(config)}, callbacks_{std::move(callbacks)},
      decoder_{config_.max_frame_bytes_} {
  if (config_.program_.empty()) {
    LOG_WARNING("pyfunction: worker program is empty");
    return;
  }

  int protocol_in[2]{};
  int protocol_out[2]{};
  int stdout_pipe[2]{};
  int stderr_pipe[2]{};

  if (::pipe2(protocol_in, 0) != 0 || ::pipe2(protocol_out, 0) != 0 ||
      ::pipe2(stdout_pipe, 0) != 0 || ::pipe2(stderr_pipe, 0) != 0) {
    LOG_WARNING("pyfunction: worker pipe creation failed (errno={})", errno);
    closeFd(&protocol_in[0]);
    closeFd(&protocol_in[1]);
    closeFd(&protocol_out[0]);
    closeFd(&protocol_out[1]);
    closeFd(&stdout_pipe[0]);
    closeFd(&stdout_pipe[1]);
    closeFd(&stderr_pipe[0]);
    closeFd(&stderr_pipe[1]);

    return;
  }

  // Parent-side ends are non-blocking so io_uring does not spin on EAGAIN and
  // the drain path stops deterministically.
  // Only the parent's ends are non-blocking; the child's ends stay blocking.
  ::fcntl(protocol_in[1], F_SETFL, O_NONBLOCK);
  ::fcntl(protocol_out[0], F_SETFL, O_NONBLOCK);
  ::fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
  ::fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

  posix_spawn_file_actions_t actions;
  bool actions_ready = false;

  if (posix_spawn_file_actions_init(&actions) == 0) {
    actions_ready = true;
    (void)posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    (void)posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    (void)posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
    // Close every end the child does not use. The two protocol ends stay at
    // their original descriptor numbers and are communicated through the
    // environment, so no dup2 dance is needed.
    (void)posix_spawn_file_actions_addclose(&actions, protocol_in[1]);
    (void)posix_spawn_file_actions_addclose(&actions, protocol_out[0]);
    (void)posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    (void)posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    (void)posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    (void)posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);
  }

  std::vector<std::string> argv_storage;
  argv_storage.push_back(config_.program_);
  for (const auto& argument : config_.arguments_) {
    argv_storage.push_back(argument);
  }

  std::vector<char*> argv;
  argv.reserve(argv_storage.size() + 1);

  for (auto& argument : argv_storage) {
    argv.push_back(argument.data());
  }

  argv.push_back(nullptr);

  std::vector<std::string> environment;
  for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
    environment.emplace_back(*entry);
  }

  for (const auto& entry : config_.environment_) {
    const auto equals = entry.find('=');
    if (equals == std::string::npos) {
      continue;
    }

    upsertEnvironment(environment, std::string_view(entry).substr(0, equals),
                      std::string_view(entry).substr(equals + 1));
  }

  upsertEnvironment(environment, "STRIJ_PYFUNCTION_FD_IN", std::to_string(protocol_in[0]));
  upsertEnvironment(environment, "STRIJ_PYFUNCTION_FD_OUT", std::to_string(protocol_out[1]));
  upsertEnvironment(environment, "STRIJ_PYFUNCTION_MAX_FRAME_BYTES",
                    std::to_string(config_.max_frame_bytes_));

  std::vector<char*> envp;
  envp.reserve(environment.size() + 1);
  for (auto& entry : environment) {
    envp.push_back(entry.data());
  }

  envp.push_back(nullptr);

  // posix_spawnp (not posix_spawn) so a bare interpreter name such as
  // "python3" is resolved through PATH.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int rc =
      config_.program_.find('/') == std::string::npos
          ? posix_spawnp(&pid_, config_.program_.c_str(), actions_ready ? &actions : nullptr,
                         nullptr, argv.data(), envp.data())
          : posix_spawn(&pid_, config_.program_.c_str(), actions_ready ? &actions : nullptr,
                        nullptr, argv.data(), envp.data());

  if (actions_ready) {
    posix_spawn_file_actions_destroy(&actions);
  }

  closeFd(&protocol_in[0]);
  closeFd(&protocol_out[1]);
  closeFd(&stdout_pipe[1]);
  closeFd(&stderr_pipe[1]);

  if (rc != 0) {
    LOG_WARNING("pyfunction: posix_spawn('{}') failed ({})", config_.program_, std::strerror(rc));
    closeFd(&protocol_in[1]);
    closeFd(&protocol_out[0]);
    closeFd(&stdout_pipe[0]);
    closeFd(&stderr_pipe[0]);
    pid_ = -1;

    return;
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  pidfd_ = static_cast<int>(::syscall(SYS_pidfd_open, pid_, 0));
  if (pidfd_ < 0) {
    LOG_WARNING("pyfunction: pidfd_open failed (errno={})", errno);
    ::kill(pid_, SIGKILL);
    (void)::waitpid(pid_, nullptr, 0);
    closeFd(&protocol_in[1]);
    closeFd(&protocol_out[0]);
    closeFd(&stdout_pipe[0]);
    closeFd(&stderr_pipe[0]);
    pid_ = -1;

    return;
  }

  protocol_write_fd_ = protocol_in[1];
  protocol_read_fd_ = protocol_out[0];
  stdout_fd_ = stdout_pipe[0];
  stderr_fd_ = stderr_pipe[0];
  ok_ = true;
}

WorkerProcess::~WorkerProcess() {
  closeFd(&protocol_write_fd_);
  closeFd(&protocol_read_fd_);
  closeFd(&stdout_fd_);
  closeFd(&stderr_fd_);
  closeFd(&pidfd_);

  if (pid_ > 0) {
    ::kill(pid_, SIGKILL);
    (void)::waitpid(pid_, nullptr, 0);
  }
}

void WorkerProcess::Start() {
  if (!ok_ || lifecycle_ != Lifecycle::kNew) {
    return;
  }

  lifecycle_ = Lifecycle::kRunning;

  WorkerFrame hello;
  hello.mutable_hello()->set_version(kProtocolVersion);
  Send(hello);

  dispatcher_->PrepareRead(this, kProtocolRead, protocol_read_fd_,
                           std::span<std::byte>(protocol_buffer_), -1);
  dispatcher_->PrepareRead(this, kStdoutRead, stdout_fd_, std::span<std::byte>(stdout_buffer_), -1);
  dispatcher_->PrepareRead(this, kStderrRead, stderr_fd_, std::span<std::byte>(stderr_buffer_), -1);
  dispatcher_->PreparePoll(this, kExitPoll, pidfd_, POLLIN);
}

void WorkerProcess::Send(const WorkerFrame& frame) {
  if (!ok_ || lifecycle_ != Lifecycle::kRunning || protocol_write_fd_ < 0) {
    return;
  }

  std::string serialized;
  if (!frame.SerializeToString(&serialized)) {
    LOG_WARNING("pyfunction: failed to serialize a worker frame");
    return;
  }

  auto encoded =
      EncodeFrame(std::span<const std::byte>(reinterpret_cast<const std::byte*>(serialized.data()),
                                             serialized.size()),
                  config_.max_frame_bytes_);
  if (!encoded.ok()) {
    strij::logging::write_stderr_fallback(strij::logging::LogEntry::WARNING,
                                          std::source_location::current(), "pyfunction: {}",
                                          encoded.status().message());
    failProtocol("outbound frame too large");
    return;
  }

  LOG_DEBUG("pyfunction worker {}: queued frame ({} bytes), write_state={}", pid_, encoded->size(),
            static_cast<int>(write_state_));
  write_queue_.push_back(std::move(*encoded));
  startNextWrite();
}

void WorkerProcess::Kill() {
  if (pid_ > 0) {
    ::kill(pid_, SIGKILL);
  }
}

void WorkerProcess::HandleCompletion(uint8_t tag, int res, uint32_t /*flags*/) {
  switch (tag) {
  case kProtocolRead:
    handleProtocolRead(res);
    break;
  case kProtocolWrite:
    handleProtocolWrite(res);
    break;
  case kStdoutRead:
    handleOutputRead(res, kStdoutRead, stdout_buffer_, stdout_done_);
    break;
  case kStderrRead:
    handleOutputRead(res, kStderrRead, stderr_buffer_, stderr_done_);
    break;
  case kExitPoll:
    handleExitPoll();
    break;
  case kProtocolWritePoll:
    handleProtocolWritePoll();
    break;
  default:
    break;
  }
}

void WorkerProcess::startNextWrite() {
  if (write_state_ != WriteState::kIdle || write_queue_.empty() || protocol_write_fd_ < 0) {
    return;
  }
  write_state_ = WriteState::kInFlight;
  const auto& frame = write_queue_.front();
  LOG_DEBUG("pyfunction worker {}: writing frame offset={} size={}", pid_, write_offset_,
            frame.size() - write_offset_);
  dispatcher_->PrepareWrite(
      this, kProtocolWrite, protocol_write_fd_,
      std::span<const std::byte>(frame.data() + static_cast<std::ptrdiff_t>(write_offset_),
                                 frame.size() - write_offset_),
      -1);
}

void WorkerProcess::handleProtocolWrite(int res) {
  LOG_DEBUG("pyfunction worker {}: write completion res={}", pid_, res);
  if (res > 0) {
    // The io_uring write is no longer in flight; either requeue the remainder
    // of this frame or move on to the next one.
    write_state_ = WriteState::kIdle;
    write_offset_ += static_cast<std::size_t>(res);

    if (write_offset_ < write_queue_.front().size()) {
      startNextWrite();
      maybeFinish();

      return;
    }

    write_queue_.pop_front();
    write_offset_ = 0;
    startNextWrite();
    maybeFinish();

    return;
  }

  if (res == -EAGAIN) {
    // The pipe is full. Do not spin the event loop on immediate resubmits:
    // wait until the worker has drained enough for another write.
    write_state_ = WriteState::kIdle;
    armWritePoll();

    return;
  }

  LOG_WARNING("pyfunction: worker protocol write failed (res={})", res);
  write_state_ = WriteState::kIdle;
  write_queue_.clear();
  failProtocol("protocol write failed");
  maybeFinish();
}

void WorkerProcess::armWritePoll() {
  if (write_state_ != WriteState::kIdle || protocol_write_fd_ < 0 || write_queue_.empty()) {
    return;
  }

  write_state_ = WriteState::kWaitingForWritable;
  dispatcher_->PreparePoll(this, kProtocolWritePoll, protocol_write_fd_, POLLOUT);
}

void WorkerProcess::handleProtocolWritePoll() {
  write_state_ = WriteState::kIdle;
  startNextWrite();
  maybeFinish();
}

void WorkerProcess::handleProtocolRead(int res) {
  if (res > 0) {
    auto status = decoder_.Append(
        std::span<const std::byte>(protocol_buffer_.data(), static_cast<std::size_t>(res)));
    if (!status.ok()) {
      strij::logging::write_stderr_fallback(strij::logging::LogEntry::WARNING,
                                            std::source_location::current(), "pyfunction: {}",
                                            status.message());
      failProtocol("protocol frame bound exceeded");
      protocol_read_done_ = true;
      closeFd(&protocol_read_fd_);
      maybeFinish();

      return;
    }

    for (;;) {
      auto payload = decoder_.Next();
      if (!payload.has_value()) {
        break;
      }

      WorkerFrame frame;

      if (!frame.ParseFromArray(payload->data(), static_cast<int>(payload->size()))) {
        LOG_WARNING("pyfunction: worker sent an unparsable frame");
        failProtocol("protocol parse failure");
        protocol_read_done_ = true;
        closeFd(&protocol_read_fd_);
        maybeFinish();

        return;
      }

      if (frame.has_ready()) {
        if (callbacks_.on_ready_) {
          callbacks_.on_ready_(frame.ready().version());
        }
      } else if (callbacks_.on_frame_) {
        callbacks_.on_frame_(frame);
      }
    }

    if (protocol_read_fd_ >= 0) {
      dispatcher_->PrepareRead(this, kProtocolRead, protocol_read_fd_,
                               std::span<std::byte>(protocol_buffer_), -1);
    }

    return;
  }

  if (res == -EAGAIN) {
    dispatcher_->PrepareRead(this, kProtocolRead, protocol_read_fd_,
                             std::span<std::byte>(protocol_buffer_), -1);
    return;
  }

  LOG_DEBUG("pyfunction worker {}: protocol read closed res={}", pid_, res);
  protocol_read_done_ = true;
  closeFd(&protocol_read_fd_);
  maybeFinish();
}

void WorkerProcess::handleOutputRead(int res, uint8_t tag,
                                     std::array<std::byte, kReadBufferSize>& buffer, bool& done) {
  if (res > 0) {
    // The async logger packs arguments into a small fixed buffer. Cap one log
    // entry so a chatty child cannot overflow that buffer; the remainder is
    // read again on the next completion.
    constexpr std::size_t kMaxLoggedBytes = 256;
    const auto length = std::min<std::size_t>(static_cast<std::size_t>(res), kMaxLoggedBytes);
    std::string text(std::bit_cast<const char*>(buffer.data()), length);

    if (static_cast<std::size_t>(res) > length) {
      text += "...";
    }

    // Synchronous fallback formatting: worker output is untrusted and variably
    // sized, and must never be packed into the async logger's fixed buffer.
    strij::logging::write_stderr_fallback(
        strij::logging::LogEntry::INFO, std::source_location::current(),
        "pyfunction worker {} {}: {}", pid_, tag == kStdoutRead ? "stdout" : "stderr", text);
    const int fd = tag == kStdoutRead ? stdout_fd_ : stderr_fd_;
    dispatcher_->PrepareRead(this, tag, fd, std::span<std::byte>(buffer), -1);

    return;
  }

  if (res == -EAGAIN) {
    const int fd = tag == kStdoutRead ? stdout_fd_ : stderr_fd_;
    dispatcher_->PrepareRead(this, tag, fd, std::span<std::byte>(buffer), -1);

    return;
  }

  done = true;
  if (tag == kStdoutRead) {
    closeFd(&stdout_fd_);
  } else {
    closeFd(&stderr_fd_);
  }

  maybeFinish();
}

void WorkerProcess::handleExitPoll() {
  if (pid_ > 0) {
    int status = 0;
    const pid_t reaped = ::waitpid(pid_, &status, WNOHANG);

    if (reaped == 0) {
      // Spurious wakeup: keep polling until the child is actually reapable.
      dispatcher_->PreparePoll(this, kExitPoll, pidfd_, POLLIN);

      return;
    }

    if (reaped > 0) {
      const pid_t exited_pid = pid_;
      pid_ = -1;

      if (WIFSIGNALED(status)) {
        LOG_WARNING("pyfunction worker {} terminated by signal {}", exited_pid, WTERMSIG(status));
      } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        LOG_WARNING("pyfunction worker {} exited with status {}", exited_pid, WEXITSTATUS(status));
      } else if (WIFEXITED(status)) {
        LOG_DEBUG("pyfunction worker {} exited with status {}", exited_pid, WEXITSTATUS(status));
      }
    }
  }

  closeFd(&pidfd_);
  poll_fired_ = true;
  maybeFinish();
}

void WorkerProcess::failProtocol(const char* reason) {
  LOG_WARNING("pyfunction worker {}: {}", pid_, reason);
  Kill();
}

void WorkerProcess::maybeFinish() {
  if (lifecycle_ != Lifecycle::kRunning) {
    return;
  }

  if (!(protocol_read_done_ && stdout_done_ && stderr_done_ && poll_fired_) ||
      write_state_ != WriteState::kIdle) {
    return;
  }

  lifecycle_ = Lifecycle::kExited;
  closeFd(&protocol_write_fd_);
  write_queue_.clear();

  if (callbacks_.on_exit_) {
    callbacks_.on_exit_();
  }
}

} // namespace strij::nodeagent::task_handlers::pyfunction
