#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "extensions/task_handlers/task_handlers.hh"
#include "strij/event/command_handler.hh"
#include "strij/event/completable.hh"
#include "strij/event/dispatcher.hh"

namespace strij::extensions::task_handlers {

/**
 * @brief One in-flight piped_executable task: a spawned child whose stdin is
 * fed the task body and whose stdout is streamed back chunk-by-chunk.
 *
 * Implements event::Completable so the dispatcher routes per-tag completions
 * (the same pattern as io::Connection). The child is reaped via a pidfd poll
 * (D5); finality keys off process exit, not stdout EOF (D7). Teardown is
 * deferred through a DEFERRED_DELETE command to the owning handler, so no code
 * touches `this` after the erase (D6).
 */
class ChildProcess final : public event::Completable {
public:
  ChildProcess(event::Dispatcher& dispatcher, event::CommandHandler* owner, std::string task_id,
               std::string executable_path, std::vector<std::byte> stdin_body,
               std::unique_ptr<ResultSender> sender);
  ~ChildProcess() override;

  ChildProcess(const ChildProcess&) = delete;
  auto operator=(const ChildProcess&) -> ChildProcess& = delete;
  ChildProcess(ChildProcess&&) noexcept = delete;
  auto operator=(ChildProcess&&) noexcept -> ChildProcess& = delete;

  // Arms the async I/O and registers the connection-close callback. Must be
  // called exactly once, after the owning handler confirmed ok().
  void Start();

  // False when the spawn failed; the owning handler then delivers an empty
  // final result instead of registering the child.
  [[nodiscard]] auto OK() const -> bool;

  // event::Completable interface
  void HandleCompletion(uint8_t tag, int res, uint32_t flags) override;

private:
  enum Tags : uint8_t { kStdinWrite = 0, kStdoutRead = 1, kStderrRead = 2, kExitPoll = 3 };
  static constexpr std::size_t kBufferSize = 4096;

  void handleStdinWrite(int res);
  void handleStdoutRead(int res);
  void handleStderrRead(int res);
  void handleExitPoll();
  void drainOutput();
  void handleConnectionClosed();
  void maybeFinish();

  event::Dispatcher* dispatcher_;
  event::CommandHandler* owner_;
  std::string task_id_;
  std::string executable_path_;
  std::vector<std::byte> stdin_body_;
  std::size_t stdin_offset_{0};
  std::unique_ptr<ResultSender> sender_;
  std::size_t close_token_{0};

  int stdin_w_{-1};
  int stdout_r_{-1};
  int stderr_r_{-1};
  int pidfd_{-1};
  pid_t pid_{-1};
  bool ok_{false};

  std::array<std::byte, kBufferSize> stdout_buffer_{};
  std::array<std::byte, kBufferSize> stderr_buffer_{};
  std::vector<std::byte> final_body_;

  bool stdin_done_{false};
  bool stdout_done_{false};
  bool stderr_done_{false};
  bool poll_fired_{false};
  bool close_registered_{false};
  bool teardown_submitted_{false};
};

} // namespace strij::extensions::task_handlers
