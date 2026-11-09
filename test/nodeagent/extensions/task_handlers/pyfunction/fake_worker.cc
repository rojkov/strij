// A scripted worker used by the C++ unit tests. It speaks the same
// length-prefixed protobuf protocol as the real Python worker, without needing
// a Python runtime. Behavior is selected by argv[1].
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "nodeagent/extensions/task_handlers/pyfunction/pyfunction.pb.h"

namespace {

using strij::extensions::task_handlers::pyfunction::WorkerFrame;

auto fdFromEnv(const char* name) -> int {
  const char* value = std::getenv(name);
  return value == nullptr ? -1 : std::atoi(value);
}

auto readAll(int fd, void* buffer, std::size_t size) -> bool {
  auto* bytes = static_cast<char*>(buffer);
  while (size > 0) {
    const ssize_t got = ::read(fd, bytes, size);
    if (got <= 0) {
      return false;
    }
    bytes += got;
    size -= static_cast<std::size_t>(got);
  }
  return true;
}

auto writeAll(int fd, const void* buffer, std::size_t size) -> bool {
  const auto* bytes = static_cast<const char*>(buffer);
  while (size > 0) {
    const ssize_t wrote = ::write(fd, bytes, size);
    if (wrote <= 0) {
      return false;
    }
    bytes += wrote;
    size -= static_cast<std::size_t>(wrote);
  }
  return true;
}

auto sendFrame(int fd, const WorkerFrame& frame) -> bool {
  std::string payload;
  frame.SerializeToString(&payload);
  const auto length = static_cast<std::uint32_t>(payload.size());
  std::array<char, 4> header{
      static_cast<char>((length >> 24U) & 0xFFU), static_cast<char>((length >> 16U) & 0xFFU),
      static_cast<char>((length >> 8U) & 0xFFU), static_cast<char>(length & 0xFFU)};
  return writeAll(fd, header.data(), header.size()) && writeAll(fd, payload.data(), payload.size());
}

auto readFrame(int fd, WorkerFrame& frame) -> bool {
  std::array<unsigned char, 4> header{};
  if (!readAll(fd, header.data(), header.size())) {
    return false;
  }
  const std::uint32_t length = (static_cast<std::uint32_t>(header[0]) << 24U) |
                               (static_cast<std::uint32_t>(header[1]) << 16U) |
                               (static_cast<std::uint32_t>(header[2]) << 8U) |
                               static_cast<std::uint32_t>(header[3]);
  if (length > 64U * 1024U * 1024U) {
    return false;
  }
  std::string payload(length, '\0');
  if (!readAll(fd, payload.data(), payload.size())) {
    return false;
  }
  return frame.ParseFromString(payload);
}

void sendReady(int out, std::uint32_t version) {
  WorkerFrame frame;
  frame.mutable_ready()->set_pid(static_cast<std::uint32_t>(::getpid()));
  frame.mutable_ready()->set_version(version);
  sendFrame(out, frame);
}

auto echoResult(const WorkerFrame& run) -> WorkerFrame {
  WorkerFrame out;
  auto* result = out.mutable_run_result();
  result->set_task_id(run.run().task_id());
  result->set_body(run.run().body());
  result->set_is_final(true);
  result->set_status(0);
  return out;
}

} // namespace

auto main(int argc, char** argv) -> int {
  const char* env_mode = std::getenv("FAKE_WORKER_MODE");
  const std::string mode = argc > 1 ? argv[1] : (env_mode != nullptr ? env_mode : "echo");
  const char* env_seconds = std::getenv("FAKE_WORKER_SECONDS");
  const int configured_seconds = env_seconds != nullptr ? std::atoi(env_seconds) : 5;
  const int fd_in = fdFromEnv("STRIJ_PYFUNCTION_FD_IN");
  const int fd_out = fdFromEnv("STRIJ_PYFUNCTION_FD_OUT");
  if (fd_in < 0 || fd_out < 0) {
    return 2;
  }

  if (mode == "bad_version") {
    sendReady(fd_out, 999);
    std::this_thread::sleep_for(std::chrono::seconds(5));
    return 0;
  }

  if (mode == "oversized") {
    const std::array<char, 4> header{static_cast<char>(0x7F), 0, 0, 0};
    writeAll(fd_out, header.data(), header.size());
    std::this_thread::sleep_for(std::chrono::seconds(5));
    return 0;
  }

  if (mode == "delay_ready" || mode == "no_ready" || mode == "slow_run") {
    const int seconds = argc > 2 ? std::atoi(argv[2]) : configured_seconds;
    if (mode != "slow_run") {
      std::this_thread::sleep_for(std::chrono::seconds(seconds));
    }
  }

  if (mode == "slow_ready_once") {
    const char* state_path = std::getenv("FAKE_WORKER_STATE_FILE");
    bool first_attempt = false;
    if (state_path == nullptr) {
      first_attempt = true;
    } else {
      std::FILE* marker = std::fopen(state_path, "r");
      if (marker == nullptr) {
        first_attempt = true;
        marker = std::fopen(state_path, "w");
      }
      if (marker != nullptr) {
        std::fclose(marker);
      }
    }
    if (first_attempt) {
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
  }

  sendReady(fd_out, 1);

  if (mode == "exit_after_ready") {
    return 0;
  }

  bool first_run = true;
  std::vector<std::string> seen_remote_results;

  while (true) {
    WorkerFrame frame;
    if (!readFrame(fd_in, frame)) {
      const std::string message = "fake-worker: frame read failed\n";
      (void)writeAll(STDERR_FILENO, message.data(), message.size());
      return 0;
    }
    if (frame.has_shutdown()) {
      return 0;
    }
    if (frame.has_remote_result() && mode == "remote_then_finish") {
      seen_remote_results.push_back(frame.remote_result().call_id());
      continue;
    }
    if (!frame.has_run()) {
      const std::string message = "fake-worker: ignoring non-run frame\n";
      (void)writeAll(STDERR_FILENO, message.data(), message.size());
      continue;
    }
    {
      const std::string message = "fake-worker: got run " + frame.run().task_id() + "\n";
      (void)writeAll(STDERR_FILENO, message.data(), message.size());
    }

    if (mode == "stderr") {
      const std::string message = "fake-worker-stderr\n";
      (void)writeAll(STDERR_FILENO, message.data(), message.size());
    }

    if (mode == "exit_on_run") {
      return 7;
    }

    if (mode == "pid") {
      WorkerFrame out;
      auto* result = out.mutable_run_result();
      result->set_task_id(frame.run().task_id());
      result->set_body(std::to_string(::getpid()));
      result->set_is_final(true);
      result->set_status(0);
      sendFrame(fd_out, out);
      continue;
    }

    if (mode == "env") {
      const char* python_path = std::getenv("PYTHONPATH");
      WorkerFrame out;
      auto* result = out.mutable_run_result();
      result->set_task_id(frame.run().task_id());
      result->set_body(python_path == nullptr ? "" : python_path);
      result->set_is_final(true);
      result->set_status(0);
      sendFrame(fd_out, out);
      continue;
    }

    if (mode == "remote_then_exit") {
      WorkerFrame call;
      auto* remote = call.mutable_remote_call();
      remote->set_call_id("call-crash");
      remote->set_invocation(frame.run().body());
      sendFrame(fd_out, call);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      return 7;
    }

    if (mode == "remote_then_finish") {
      if (first_run) {
        first_run = false;
        WorkerFrame call;
        auto* remote = call.mutable_remote_call();
        remote->set_call_id("call-stale");
        remote->set_invocation(frame.run().body());
        sendFrame(fd_out, call);
      }

      // Report every RemoteResult the handler forwarded after the first task
      // completed. A correct handler drops them, so later tasks see "clean".
      std::string observed;
      for (const auto& call_id : seen_remote_results) {
        observed += call_id;
      }

      WorkerFrame out;
      auto* result = out.mutable_run_result();
      result->set_task_id(frame.run().task_id());
      result->set_body(observed.empty() ? "clean" : observed);
      result->set_is_final(true);
      result->set_status(0);
      sendFrame(fd_out, out);
      continue;
    }

    if (mode == "slow_run") {
      const int seconds = argc > 2 ? std::atoi(argv[2]) : configured_seconds;
      std::this_thread::sleep_for(std::chrono::seconds(seconds));
    }

    if (mode == "remote_success" || mode == "remote_error" || mode == "remote_timeout") {
      WorkerFrame call;
      auto* remote = call.mutable_remote_call();
      remote->set_call_id("call-1");
      remote->set_invocation(frame.run().body());
      (*remote->mutable_parameters())["function"] = "module:test:main";
      (*remote->mutable_requirements())["cpu"] = 2;
      sendFrame(fd_out, call);

      while (true) {
        WorkerFrame incoming;
        if (!readFrame(fd_in, incoming)) {
          return 0;
        }
        if (!incoming.has_remote_result()) {
          continue;
        }
        const auto& result = incoming.remote_result();
        WorkerFrame out;
        auto* run_result = out.mutable_run_result();
        run_result->set_task_id(frame.run().task_id());
        run_result->set_body(result.status() == 0 ? result.body() : result.reason());
        run_result->set_status(result.status());
        run_result->set_is_final(true);
        sendFrame(fd_out, out);
        break;
      }
      continue;
    }

    sendFrame(fd_out, echoResult(frame));
  }
}
