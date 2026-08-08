#include "extensions/task_handlers/piped_executable/child_process.hh"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <span>
#include <utility>

#include "core/logging/log.hh"
#include "core/task/task.pb.h"
#include "strij/event/command.hh"

extern char** environ;

namespace strij::extensions::task_handlers {

namespace {

constexpr int kInvalidFd = -1;

// Closes `*fd` and resets it to kInvalidFd.
void closeFd(int* fd) {
  if (*fd != kInvalidFd) {
    ::close(*fd);
    *fd = kInvalidFd;
  }
}

// The handler moves ownership of the sender into the child before the spawn
// runs, so a failed spawn must deliver its own empty final result.
void sendEmptyFinal(const std::string& task_id, ResultSender& sender) {
  strij::task::TaskResult result;
  result.set_id(task_id);
  result.set_is_final(true);
  sender.Send(std::move(result));
}

} // namespace

ChildProcess::ChildProcess(event::Dispatcher& dispatcher, event::CommandHandler* owner,
                           std::string task_id, std::string executable_path,
                           std::vector<std::byte> stdin_body,
                           std::unique_ptr<ResultSender> sender)
    : dispatcher_{&dispatcher}, owner_{owner}, task_id_{std::move(task_id)},
      executable_path_{std::move(executable_path)}, stdin_body_{std::move(stdin_body)},
      sender_{std::move(sender)} {
  int stdin_pipe[2]{};
  int stdout_pipe[2]{};
  int stderr_pipe[2]{};

  // O_CLOEXEC: the child must not inherit the parent's ends. O_NONBLOCK is set
  // on the ends this side keeps so io_uring never spins on EAGAIN (the kernel
  // re-arms a poll internally) and the drain at exit returns EAGAIN.
  if (::pipe2(stdin_pipe, O_CLOEXEC) != 0 || ::pipe2(stdout_pipe, O_CLOEXEC) != 0 ||
      ::pipe2(stderr_pipe, O_CLOEXEC) != 0) {
    LOG_WARNING("Child '{}': pipe creation failed (errno={})", task_id_, errno);
    closeFd(&stdin_pipe[0]);
    closeFd(&stdin_pipe[1]);
    closeFd(&stdout_pipe[0]);
    closeFd(&stdout_pipe[1]);
    closeFd(&stderr_pipe[0]);
    closeFd(&stderr_pipe[1]);
    sendEmptyFinal(task_id_, *sender_);
    return;
  }

  ::fcntl(stdin_pipe[1], F_SETFL, O_NONBLOCK);
  ::fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
  ::fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

  posix_spawn_file_actions_t actions;
  bool actions_ready = false;
  if (posix_spawn_file_actions_init(&actions) == 0) {
    actions_ready = true;
    // Redirect the child's stdio, then close the fds the child does not use.
    // The transient ends are closed in the parent right after the spawn.
    (void)posix_spawn_file_actions_adddup2(&actions, stdin_pipe[0], STDIN_FILENO);
    (void)posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    (void)posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
    (void)posix_spawn_file_actions_addclose(&actions, stdin_pipe[0]);
    (void)posix_spawn_file_actions_addclose(&actions, stdin_pipe[1]);
    (void)posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    (void)posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    (void)posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    (void)posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);
  }

  char* const argv[] = {executable_path_.data(), nullptr};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  int rc = posix_spawn(&pid_, executable_path_.c_str(), actions_ready ? &actions : nullptr,
                       nullptr, argv, environ);
  if (actions_ready) {
    posix_spawn_file_actions_destroy(&actions);
  }

  closeFd(&stdin_pipe[0]);
  closeFd(&stdout_pipe[1]);
  closeFd(&stderr_pipe[1]);

  if (rc != 0) {
    LOG_WARNING("Child '{}': posix_spawn('{}') failed (errno={})", task_id_, executable_path_,
                rc);
    closeFd(&stdin_pipe[1]);
    closeFd(&stdout_pipe[0]);
    closeFd(&stderr_pipe[0]);
    pid_ = -1;
    sendEmptyFinal(task_id_, *sender_);
    return;
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  pidfd_ = static_cast<int>(::syscall(SYS_pidfd_open, pid_, 0));
  if (pidfd_ < 0) {
    LOG_WARNING("Child '{}': pidfd_open failed (errno={})", task_id_, errno);
    ::kill(pid_, SIGKILL);
    closeFd(&stdin_pipe[1]);
    closeFd(&stdout_pipe[0]);
    closeFd(&stderr_pipe[0]);
    pid_ = -1;
    sendEmptyFinal(task_id_, *sender_);
    return;
  }

  stdin_w_ = stdin_pipe[1];
  stdout_r_ = stdout_pipe[0];
  stderr_r_ = stderr_pipe[0];
  ok_ = true;
}

ChildProcess::~ChildProcess() {
  // Only reachable after teardown (all I/O done), so no sqe references these
  // fds; closing is defensive.
  closeFd(&stdin_w_);
  closeFd(&stdout_r_);
  closeFd(&stderr_r_);
  closeFd(&pidfd_);
  if (pid_ > 0) {
    ::kill(pid_, SIGKILL);
  }
}

auto ChildProcess::OK() const -> bool { return ok_; }

void ChildProcess::Start() {
  if (!ok_) {
    return;
  }

  if (stdin_body_.empty()) {
    stdin_done_ = true;
    closeFd(&stdin_w_);
  } else {
    dispatcher_->PrepareWrite(this, kStdinWrite, stdin_w_, std::span<const std::byte>(stdin_body_),
                              -1);
  }
  dispatcher_->PrepareRead(this, kStdoutRead, stdout_r_, std::span<std::byte>(stdout_buffer_), -1);
  dispatcher_->PrepareRead(this, kStderrRead, stderr_r_, std::span<std::byte>(stderr_buffer_), -1);
  dispatcher_->PreparePoll(this, kExitPoll, pidfd_, POLLIN);

  close_token_ = sender_->RegisterOnClose([this]() { handleConnectionClosed(); });
  close_registered_ = true;
}

void ChildProcess::HandleCompletion(uint8_t tag, int res, uint32_t /*flags*/) {
  switch (tag) {
  case kStdinWrite:
    handleStdinWrite(res);
    break;
  case kStdoutRead:
    handleStdoutRead(res);
    break;
  case kStderrRead:
    handleStderrRead(res);
    break;
  case kExitPoll:
    handleExitPoll();
    break;
  default:
    break;
  }
}

void ChildProcess::handleStdinWrite(int res) {
  if (res > 0) {
    stdin_offset_ += static_cast<std::size_t>(res);
    if (stdin_offset_ < stdin_body_.size()) {
      dispatcher_->PrepareWrite(
          this, kStdinWrite, stdin_w_,
          std::span<const std::byte>(stdin_body_.data() + stdin_offset_,
                                     stdin_body_.size() - stdin_offset_),
          -1);
      return;
    }
  } else {
    LOG_WARNING("Child '{}': stdin write failed (res={}); stdin truncated", task_id_, res);
  }
  stdin_done_ = true;
  closeFd(&stdin_w_);
  maybeFinish();
}

void ChildProcess::handleStdoutRead(int res) {
  if (res > 0 && !poll_fired_) {
    strij::task::TaskResult result;
    result.set_id(task_id_);
    // Absence of is_final means final (proto3 optional), so streaming chunks
    // must set it explicitly or the gateway closes the connection early.
    result.set_is_final(false);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    result.set_body(std::string(reinterpret_cast<const char*>(stdout_buffer_.data()),
                                static_cast<std::size_t>(res)));
    sender_->Send(std::move(result));
    dispatcher_->PrepareRead(this, kStdoutRead, stdout_r_, std::span<std::byte>(stdout_buffer_),
                             -1);
    return;
  }
  if (res < 0 && res == -EAGAIN) {
    dispatcher_->PrepareRead(this, kStdoutRead, stdout_r_, std::span<std::byte>(stdout_buffer_),
                             -1);
    return;
  }
  // EOF/error, or data that arrived after the final result was sent (dropped,
  // per the known v1 edge in the design): stop reading.
  stdout_done_ = true;
  closeFd(&stdout_r_);
  maybeFinish();
}

void ChildProcess::handleStderrRead(int res) {
  if (res > 0 && !poll_fired_) {
    LOG_INFO("Child '{}' stderr: {}", task_id_,
             std::string_view(reinterpret_cast<const char*>(stderr_buffer_.data()),
                              static_cast<std::size_t>(res)));
    dispatcher_->PrepareRead(this, kStderrRead, stderr_r_, std::span<std::byte>(stderr_buffer_),
                             -1);
    return;
  }
  if (res < 0 && res == -EAGAIN) {
    dispatcher_->PrepareRead(this, kStderrRead, stderr_r_, std::span<std::byte>(stderr_buffer_),
                             -1);
    return;
  }
  stderr_done_ = true;
  closeFd(&stderr_r_);
  maybeFinish();
}

void ChildProcess::handleExitPoll() {
  int status = 0;
  pid_t reaped = -1;
  if (pid_ > 0) {
    // The pidfd poll already fired, so this waitpid(WNOHANG) cannot block.
    reaped = ::waitpid(pid_, &status, WNOHANG);
    pid_ = -1;
  }
  closeFd(&pidfd_);
  poll_fired_ = true;

  if (reaped > 0 && WIFEXITED(status)) {
    LOG_DEBUG("Child '{}' exited with status {}", task_id_, WEXITSTATUS(status));
  }

  drainOutput();

  strij::task::TaskResult result;
  result.set_id(task_id_);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  result.set_body(std::string(reinterpret_cast<const char*>(final_body_.data()),
                              final_body_.size()));
  result.set_is_final(true);
  sender_->Send(std::move(result));

  maybeFinish();
}

void ChildProcess::drainOutput() {
  // The read ends are O_NONBLOCK, so each read returns whatever the child wrote
  // before exiting, then EAGAIN once the pipe is empty. Output written by
  // grandchildren after this drain is dropped (known v1 edge).
  for (;;) {
    ssize_t n = ::read(stdout_r_, stdout_buffer_.data(), stdout_buffer_.size());
    if (n > 0) {
      final_body_.insert(final_body_.end(), stdout_buffer_.begin(),
                         stdout_buffer_.begin() + n);
      continue;
    }
    if (n == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    }
    break;
  }
  for (;;) {
    ssize_t n = ::read(stderr_r_, stderr_buffer_.data(), stderr_buffer_.size());
    if (n > 0) {
      LOG_INFO("Child '{}' stderr: {}", task_id_,
               std::string_view(reinterpret_cast<const char*>(stderr_buffer_.data()),
                                static_cast<std::size_t>(n)));
      continue;
    }
    if (n == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    }
    break;
  }
}

void ChildProcess::handleConnectionClosed() {
  // The gateway link is gone; results can no longer be delivered. Kill the
  // child; the pidfd poll then drives the normal teardown path.
  if (pid_ > 0) {
    ::kill(pid_, SIGKILL);
  }
}

void ChildProcess::maybeFinish() {
  if (!(stdin_done_ && stdout_done_ && stderr_done_ && poll_fired_) || teardown_submitted_) {
    return;
  }
  teardown_submitted_ = true;
  if (close_registered_) {
    sender_->UnregisterOnClose(close_token_);
    close_registered_ = false;
  }
  dispatcher_->SubmitCommand({.type_ = event::Command::DEFERRED_DELETE,
                              .destination_ = owner_,
                              .args_ = this});
}

} // namespace strij::extensions::task_handlers
