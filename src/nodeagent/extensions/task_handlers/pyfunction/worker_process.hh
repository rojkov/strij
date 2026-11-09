#pragma once

#include <sys/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "nodeagent/extensions/task_handlers/pyfunction/frame.hh"
#include "nodeagent/extensions/task_handlers/pyfunction/pyfunction.pb.h"
#include "strij/event/command_handler.hh"
#include "strij/event/completable.hh"
#include "strij/event/dispatcher.hh"

namespace strij::nodeagent::task_handlers::pyfunction {

using WorkerFrame = ::strij::extensions::task_handlers::pyfunction::WorkerFrame;

struct WorkerSpawnConfig {
  // Program to execute. With `arguments_` empty this is the worker executable;
  // otherwise it is the Python interpreter and `arguments_` carries `-m
  // <module>` (or any operator-supplied arguments).
  std::string program_;
  std::vector<std::string> arguments_;
  // KEY=VALUE additions/overrides for the child environment.
  std::vector<std::string> environment_;
  std::size_t max_frame_bytes_{kDefaultMaxFrameBytes};
};

struct WorkerCallbacks {
  std::move_only_function<void(std::uint32_t version)> on_ready_;
  std::move_only_function<void(const WorkerFrame& frame)> on_frame_;
  std::move_only_function<void()> on_exit_;
};

// One persistent Python worker process. It owns the dedicated protocol pipes,
// captures stdout/stderr into the node log, detects exit through a pidfd, and
// serializes protocol writes (one in-flight write, later frames queue).
//
// Lifecycle: the owner constructs it (Ok() reports spawn success), calls
// Start() once, then treats on_exit as terminal. The owner must keep this
// object alive until on_exit runs; all pending io_uring operations have
// completed by then.
class WorkerProcess final : public event::Completable {
public:
  WorkerProcess(event::Dispatcher& dispatcher, WorkerSpawnConfig config, WorkerCallbacks callbacks);
  ~WorkerProcess() override;

  WorkerProcess(const WorkerProcess&) = delete;
  auto operator=(const WorkerProcess&) -> WorkerProcess& = delete;
  WorkerProcess(WorkerProcess&&) noexcept = delete;
  auto operator=(WorkerProcess&&) noexcept -> WorkerProcess& = delete;

  [[nodiscard]] auto Ok() const -> bool { return ok_; }
  [[nodiscard]] auto Pid() const -> pid_t { return pid_; }

  void Start();
  void Send(const WorkerFrame& frame);
  void Kill();

  void HandleCompletion(uint8_t tag, int res, uint32_t flags) override;

private:
  enum Tags : uint8_t {
    kProtocolRead = 0,
    kProtocolWrite = 1,
    kStdoutRead = 2,
    kStderrRead = 3,
    kExitPoll = 4,
    kProtocolWritePoll = 5
  };

  // The write path is a small state machine: at most one write SQE is in
  // flight, and EAGAIN waits for POLLOUT instead of spinning or stalling.
  enum class WriteState : uint8_t { kIdle = 0, kInFlight = 1, kWaitingForWritable = 2 };

  // One-shot process lifecycle. on_exit fires exactly once, after Start() and
  // after every drain completes.
  enum class Lifecycle : uint8_t { kNew = 0, kRunning = 1, kExited = 2 };

  static constexpr std::size_t kReadBufferSize = 64 * 1024;

  void handleProtocolRead(int res);
  void handleProtocolWrite(int res);
  void handleProtocolWritePoll();
  void armWritePoll();
  void handleOutputRead(int res, uint8_t tag, std::array<std::byte, kReadBufferSize>& buffer,
                        bool& done);
  void handleExitPoll();
  void startNextWrite();
  void failProtocol(const char* reason);
  void maybeFinish();

  event::Dispatcher* dispatcher_;
  WorkerSpawnConfig config_;
  WorkerCallbacks callbacks_;

  int protocol_write_fd_{-1};
  int protocol_read_fd_{-1};
  int stdout_fd_{-1};
  int stderr_fd_{-1};
  int pidfd_{-1};
  pid_t pid_{-1};
  bool ok_{false};

  FrameDecoder decoder_;
  std::array<std::byte, kReadBufferSize> protocol_buffer_{};
  std::array<std::byte, kReadBufferSize> stdout_buffer_{};
  std::array<std::byte, kReadBufferSize> stderr_buffer_{};

  std::deque<std::vector<std::byte>> write_queue_;
  std::size_t write_offset_{0};
  WriteState write_state_{WriteState::kIdle};

  bool protocol_read_done_{false};
  bool stdout_done_{false};
  bool stderr_done_{false};
  bool poll_fired_{false};
  Lifecycle lifecycle_{Lifecycle::kNew};
};

} // namespace strij::nodeagent::task_handlers::pyfunction
