#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "common/core/event/dispatcher_impl.hh"
#include "common/task/task.pb.h"
#include "gtest/gtest.h"
#include "nodeagent/extensions/task_handlers/pyfunction/deadline_timer.hh"
#include "nodeagent/extensions/task_handlers/pyfunction/pyfunction_task_handler.hh"
#include "strij/gateway/result_receiver_storage.hh"
#include "strij/nodeagent/child_task_submitter.hh"
#include "strij/nodeagent/task_handlers.hh"

namespace strij::nodeagent::task_handlers::pyfunction {
namespace {

auto fakeWorkerPath() -> std::string {
  const char* srcdir = std::getenv("TEST_SRCDIR");
  const char* workspace = std::getenv("TEST_WORKSPACE");
  if (srcdir == nullptr || workspace == nullptr) {
    return {};
  }
  return std::string(srcdir) + "/" + workspace +
         "/test/nodeagent/extensions/task_handlers/pyfunction/fake_worker";
}

void setFakeWorker(const char* mode, int seconds = 5) {
  ::setenv("FAKE_WORKER_MODE", mode, 1);
  ::setenv("FAKE_WORKER_SECONDS", std::to_string(seconds).c_str(), 1);
}

struct RecordingState {
  struct Result {
    std::string body;
    task::TaskStatus status{task::TASK_STATUS_OK};
    bool is_final{false};
  };

  std::vector<Result> results;
  std::vector<std::move_only_function<void()>> close_callbacks;
  std::function<void()> on_final;

  auto finalCount() const -> std::size_t {
    std::size_t count = 0;
    for (const auto& result : results) {
      count += result.is_final ? 1 : 0;
    }
    return count;
  }
};

class RecordingSender final : public ResultSender {
public:
  explicit RecordingSender(std::shared_ptr<RecordingState> state) : state_{std::move(state)} {}

  void Send(task::TaskResult result) override {
    const bool final = !result.has_is_final() || result.is_final();
    state_->results.push_back(
        {result.body(), result.has_status() ? result.status() : task::TASK_STATUS_OK, final});
    if (final && state_->on_final) {
      state_->on_final();
    }
  }

  auto RegisterOnClose(std::move_only_function<void()> close_cb) -> std::size_t override {
    state_->close_callbacks.emplace_back(
        [callback = std::move(close_cb)]() mutable { callback(); });
    return state_->close_callbacks.size() - 1;
  }

  void UnregisterOnClose(std::size_t token) override {
    if (token < state_->close_callbacks.size()) {
      state_->close_callbacks[token] = {};
    }
  }

private:
  std::shared_ptr<RecordingState> state_;
};

class FakeChildTaskSubmitter final : public ChildTaskSubmitter {
public:
  using SubmitHandler = std::function<void(task::Task, gateway::ResultReceiverPtr)>;

  void Submit(task::Task task, gateway::ResultReceiverPtr receiver) override {
    tasks.push_back(task);
    if (on_submit) {
      on_submit(std::move(task), std::move(receiver));
    } else {
      receivers.push_back(std::move(receiver));
    }
  }

  SubmitHandler on_submit;
  std::vector<task::Task> tasks;
  std::vector<gateway::ResultReceiverPtr> receivers;
};

class Watchdog {
public:
  explicit Watchdog(event::Dispatcher& dispatcher)
      : timer_{dispatcher, [&dispatcher]() { dispatcher.Shutdown(); }} {
    timer_.Schedule(absl::Now() + absl::Seconds(20));
  }

private:
  DeadlineTimer timer_;
};

auto makeTask(std::string id, std::string body) -> task::Task {
  task::Task task;
  task.set_id(std::move(id));
  task.set_type("pyfunction");
  task.set_body(std::move(body));
  return task;
}

auto makeConfig() -> PyFunctionConfig {
  PyFunctionConfig config;
  config.concurrency_ = 1;
  config.worker_executable_ = fakeWorkerPath();
  config.startup_timeout_ = absl::Seconds(10);
  config.remote_call_timeout_ = absl::Seconds(10);
  config.max_frame_bytes_ = 1U << 20U;
  return config;
}

auto makeRecordingSender(std::shared_ptr<RecordingState> state) -> ResultSenderPtr {
  return std::make_unique<RecordingSender>(std::move(state));
}

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(PyFunctionHandlerTest, AssignsToIdleWorkerAndSendsOneFinalResult) {
  setFakeWorker("echo");
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  PyFunctionTaskHandler handler(dispatcher, submitter, makeConfig());

  auto state = std::make_shared<RecordingState>();
  state->on_final = [&dispatcher]() { dispatcher.Shutdown(); };
  handler.HandleTask(makeTask("task-ok", "hello"), makeRecordingSender(state));
  dispatcher.Run();

  ASSERT_EQ(state->results.size(), 1U);
  EXPECT_TRUE(state->results[0].is_final);
  EXPECT_EQ(state->results[0].status, task::TASK_STATUS_OK);
  EXPECT_EQ(state->results[0].body, "hello");
  EXPECT_EQ(state->finalCount(), 1U);
  ASSERT_FALSE(state->close_callbacks.empty());
  EXPECT_FALSE(state->close_callbacks[0]);
}

TEST(PyFunctionHandlerTest, ReturnsUnavailableInsteadOfQueueing) {
  setFakeWorker("delay_ready", 5);
  event::DispatcherImpl dispatcher;
  FakeChildTaskSubmitter submitter;
  PyFunctionTaskHandler handler(dispatcher, submitter, makeConfig());

  auto first = std::make_shared<RecordingState>();
  handler.HandleTask(makeTask("task-first", "first"), makeRecordingSender(first));

  auto second = std::make_shared<RecordingState>();
  handler.HandleTask(makeTask("task-second", "second"), makeRecordingSender(second));

  ASSERT_EQ(second->results.size(), 1U);
  EXPECT_EQ(second->results[0].status, task::TASK_STATUS_UNAVAILABLE);
  EXPECT_TRUE(second->results[0].is_final);
  EXPECT_TRUE(first->results.empty());

  dispatcher.Shutdown();
  dispatcher.Run();
}

TEST(PyFunctionHandlerTest, StartingWorkerHoldsItsAssignedTask) {
  setFakeWorker("delay_ready", 1);
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  PyFunctionTaskHandler handler(dispatcher, submitter, makeConfig());

  auto state = std::make_shared<RecordingState>();
  state->on_final = [&dispatcher]() { dispatcher.Shutdown(); };
  handler.HandleTask(makeTask("task-held", "held"), makeRecordingSender(state));

  // The worker is still starting, so no result can exist yet.
  EXPECT_TRUE(state->results.empty());
  dispatcher.Run();

  ASSERT_EQ(state->results.size(), 1U);
  EXPECT_EQ(state->results[0].body, "held");
  EXPECT_EQ(state->results[0].status, task::TASK_STATUS_OK);
}

TEST(PyFunctionHandlerTest, CrashFailsInFlightWithInternalAndDoesNotRetry) {
  setFakeWorker("exit_on_run");
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  PyFunctionTaskHandler handler(dispatcher, submitter, makeConfig());

  auto state = std::make_shared<RecordingState>();
  state->on_final = [&dispatcher]() { dispatcher.Shutdown(); };
  handler.HandleTask(makeTask("task-crash", "payload"), makeRecordingSender(state));
  dispatcher.Run();

  ASSERT_EQ(state->finalCount(), 1U);
  EXPECT_EQ(state->results[0].status, task::TASK_STATUS_INTERNAL);
}

TEST(PyFunctionHandlerTest, TaskTimeoutKillsWorkerAndReportsDeadlineExceeded) {
  setFakeWorker("slow_run", 5);
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  auto config = makeConfig();
  config.task_timeout_ = absl::Milliseconds(100);
  PyFunctionTaskHandler handler(dispatcher, submitter, config);

  auto state = std::make_shared<RecordingState>();
  state->on_final = [&dispatcher]() { dispatcher.Shutdown(); };
  handler.HandleTask(makeTask("task-slow", "payload"), makeRecordingSender(state));
  dispatcher.Run();

  ASSERT_EQ(state->finalCount(), 1U);
  EXPECT_EQ(state->results[0].status, task::TASK_STATUS_DEADLINE_EXCEEDED);
}

TEST(PyFunctionHandlerTest, StartupTimeoutReportsDeadlineExceededAndRestarts) {
  const std::string marker = "/tmp/strij-pyfunction-restart-" + std::to_string(::getpid());
  std::remove(marker.c_str());
  setFakeWorker("slow_ready_once");
  ::setenv("FAKE_WORKER_STATE_FILE", marker.c_str(), 1);

  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  auto config = makeConfig();
  config.startup_timeout_ = absl::Milliseconds(100);
  PyFunctionTaskHandler handler(dispatcher, submitter, config);

  auto first = std::make_shared<RecordingState>();
  auto second = std::make_shared<RecordingState>();
  std::unique_ptr<DeadlineTimer> next;
  next = std::make_unique<DeadlineTimer>(dispatcher, [&]() {
    handler.HandleTask(makeTask("task-startup-restarted", "payload"), makeRecordingSender(second));
  });
  first->on_final = [&]() {
    if (first->finalCount() == 1) {
      next->Schedule(absl::Now() + absl::Milliseconds(500));
    }
  };
  second->on_final = [&]() { dispatcher.Shutdown(); };

  handler.HandleTask(makeTask("task-startup", "payload"), makeRecordingSender(first));
  dispatcher.Run();

  ::unsetenv("FAKE_WORKER_STATE_FILE");
  std::remove(marker.c_str());

  ASSERT_EQ(first->finalCount(), 1U);
  EXPECT_EQ(first->results[0].status, task::TASK_STATUS_DEADLINE_EXCEEDED);
  ASSERT_EQ(second->finalCount(), 1U);
  EXPECT_EQ(second->results[0].status, task::TASK_STATUS_OK);
  EXPECT_EQ(second->results[0].body, "payload");
}

TEST(PyFunctionHandlerTest, RemoteCallIsSubmittedAsChildTask) {
  setFakeWorker("remote_success");
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  submitter.on_submit = [](task::Task /*task*/, gateway::ResultReceiverPtr receiver) {
    constexpr std::string_view kChildResult = "child-result";
    receiver->Deliver(std::as_bytes(std::span(kChildResult.data(), kChildResult.size())), true);
  };
  PyFunctionTaskHandler handler(dispatcher, submitter, makeConfig());

  auto state = std::make_shared<RecordingState>();
  state->on_final = [&dispatcher]() { dispatcher.Shutdown(); };
  handler.HandleTask(makeTask("task-parent", "child-body"), makeRecordingSender(state));
  dispatcher.Run();

  ASSERT_EQ(submitter.tasks.size(), 1U);
  EXPECT_EQ(submitter.tasks[0].type(), "pyfunction");
  EXPECT_EQ(submitter.tasks[0].body(), "child-body");
  EXPECT_EQ(submitter.tasks[0].parameters().at("function"), "module:test:main");
  EXPECT_EQ(submitter.tasks[0].requirements().resources().at("cpu"), 2U);

  ASSERT_EQ(state->finalCount(), 1U);
  EXPECT_EQ(state->results[0].status, task::TASK_STATUS_OK);
  EXPECT_EQ(state->results[0].body, "child-result");
}

TEST(PyFunctionHandlerTest, RemoteChildErrorReturnsToWorker) {
  setFakeWorker("remote_error");
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  submitter.on_submit = [](task::Task /*task*/, gateway::ResultReceiverPtr receiver) {
    receiver->DeliverError("capacity denied", task::TASK_STATUS_CAPACITY_REFUSED);
  };
  PyFunctionTaskHandler handler(dispatcher, submitter, makeConfig());

  auto state = std::make_shared<RecordingState>();
  state->on_final = [&dispatcher]() { dispatcher.Shutdown(); };
  handler.HandleTask(makeTask("task-parent-error", "body"), makeRecordingSender(state));
  dispatcher.Run();

  ASSERT_EQ(state->finalCount(), 1U);
  EXPECT_EQ(state->results[0].status, task::TASK_STATUS_CAPACITY_REFUSED);
  EXPECT_EQ(state->results[0].body, "capacity denied");
}

TEST(PyFunctionHandlerTest, RemoteChildEmptyResultIsDelivered) {
  setFakeWorker("remote_success");
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  submitter.on_submit = [](task::Task /*task*/, gateway::ResultReceiverPtr receiver) {
    receiver->Deliver(std::span<const std::byte>{}, true);
  };
  PyFunctionTaskHandler handler(dispatcher, submitter, makeConfig());

  auto state = std::make_shared<RecordingState>();
  state->on_final = [&dispatcher]() { dispatcher.Shutdown(); };
  handler.HandleTask(makeTask("task-empty-child", "body"), makeRecordingSender(state));
  dispatcher.Run();

  ASSERT_EQ(state->finalCount(), 1U);
  EXPECT_EQ(state->results[0].status, task::TASK_STATUS_OK);
  EXPECT_TRUE(state->results[0].body.empty());
}

TEST(PyFunctionHandlerTest, CompletedTaskDropsOutstandingRemoteCalls) {
  setFakeWorker("remote_then_finish");
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  PyFunctionTaskHandler handler(dispatcher, submitter, makeConfig());

  auto first = std::make_shared<RecordingState>();
  auto second = std::make_shared<RecordingState>();
  std::unique_ptr<DeadlineTimer> next;
  next = std::make_unique<DeadlineTimer>(dispatcher, [&]() {
    ASSERT_EQ(submitter.receivers.size(), 1U);
    constexpr std::string_view kLateChildResult = "late-child-result";
    submitter.receivers[0]->Deliver(
        std::as_bytes(std::span(kLateChildResult.data(), kLateChildResult.size())), true);
    handler.HandleTask(makeTask("task-after-stale-call", "second"), makeRecordingSender(second));
  });
  first->on_final = [&]() {
    if (first->finalCount() == 1) {
      next->Schedule(absl::Now() + absl::Milliseconds(100));
    }
  };
  second->on_final = [&]() { dispatcher.Shutdown(); };

  handler.HandleTask(makeTask("task-stale-parent", "first"), makeRecordingSender(first));
  dispatcher.Run();

  ASSERT_EQ(first->finalCount(), 1U);
  EXPECT_EQ(first->results[0].status, task::TASK_STATUS_OK);
  ASSERT_EQ(second->finalCount(), 1U);
  EXPECT_EQ(second->results[0].status, task::TASK_STATUS_OK);
  // The fake worker reports any RemoteResult the handler forwarded for the
  // finished task; a dropped late outcome leaves the body "clean".
  EXPECT_EQ(second->results[0].body, "clean");
}

TEST(PyFunctionHandlerTest, RemoteCallTimeoutReleasesWorker) {
  setFakeWorker("remote_timeout");
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  auto config = makeConfig();
  config.remote_call_timeout_ = absl::Milliseconds(100);
  PyFunctionTaskHandler handler(dispatcher, submitter, config);

  auto state = std::make_shared<RecordingState>();
  state->on_final = [&dispatcher]() { dispatcher.Shutdown(); };
  handler.HandleTask(makeTask("task-remote-timeout", "body"), makeRecordingSender(state));
  dispatcher.Run();

  ASSERT_EQ(state->finalCount(), 1U);
  EXPECT_EQ(state->results[0].status, task::TASK_STATUS_DEADLINE_EXCEEDED);
}

TEST(PyFunctionHandlerTest, ClientDisconnectKillsWorkerAndFreesSlot) {
  setFakeWorker("slow_run", 1);
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  PyFunctionTaskHandler handler(dispatcher, submitter, makeConfig());

  auto first = std::make_shared<RecordingState>();
  handler.HandleTask(makeTask("task-disconnect", "first"), makeRecordingSender(first));

  auto second = std::make_shared<RecordingState>();
  second->on_final = [&dispatcher]() { dispatcher.Shutdown(); };

  int step = 0;
  std::unique_ptr<DeadlineTimer> control;
  control = std::make_unique<DeadlineTimer>(dispatcher, [&]() {
    if (step == 0) {
      step = 1;
      ASSERT_FALSE(first->close_callbacks.empty());
      ASSERT_TRUE(first->close_callbacks[0]);
      first->close_callbacks[0]();
      control->Schedule(absl::Now() + absl::Milliseconds(300));
      return;
    }
    handler.HandleTask(makeTask("task-second", "second"), makeRecordingSender(second));
  });
  control->Schedule(absl::Now() + absl::Milliseconds(100));

  dispatcher.Run();

  EXPECT_TRUE(first->results.empty());
  ASSERT_EQ(second->finalCount(), 1U);
  EXPECT_EQ(second->results[0].status, task::TASK_STATUS_OK);
  EXPECT_EQ(second->results[0].body, "second");
}

TEST(PyFunctionHandlerTest, ReusesIdleWorkerForNextTask) {
  setFakeWorker("pid");
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  PyFunctionTaskHandler handler(dispatcher, submitter, makeConfig());

  auto first = std::make_shared<RecordingState>();
  auto second = std::make_shared<RecordingState>();
  std::unique_ptr<DeadlineTimer> next;
  next = std::make_unique<DeadlineTimer>(dispatcher, [&]() {
    handler.HandleTask(makeTask("task-reuse-2", "second"), makeRecordingSender(second));
  });
  first->on_final = [&]() {
    if (first->finalCount() == 1) {
      next->Schedule(absl::Now() + absl::Milliseconds(50));
    }
  };
  second->on_final = [&]() { dispatcher.Shutdown(); };

  handler.HandleTask(makeTask("task-reuse-1", "first"), makeRecordingSender(first));
  dispatcher.Run();

  ASSERT_EQ(first->finalCount(), 1U);
  ASSERT_EQ(second->finalCount(), 1U);
  EXPECT_EQ(first->results[0].status, task::TASK_STATUS_OK);
  EXPECT_EQ(second->results[0].status, task::TASK_STATUS_OK);
  // The fake worker's "pid" mode reports its source process, so equal bodies
  // prove both tasks ran in the same persistent worker.
  EXPECT_EQ(first->results[0].body, second->results[0].body);
}

TEST(PyFunctionHandlerTest, PrependsConfiguredPythonPaths) {
  const char* previous = std::getenv("PYTHONPATH");
  const bool had_previous = previous != nullptr;
  const std::string saved = had_previous ? previous : "";
  ::setenv("PYTHONPATH", "/inherited", 1);

  setFakeWorker("env");
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  auto config = makeConfig();
  config.python_paths_ = {"/configured"};
  PyFunctionTaskHandler handler(dispatcher, submitter, config);

  auto state = std::make_shared<RecordingState>();
  state->on_final = [&dispatcher]() { dispatcher.Shutdown(); };
  handler.HandleTask(makeTask("task-python-path", "body"), makeRecordingSender(state));
  dispatcher.Run();

  if (had_previous) {
    ::setenv("PYTHONPATH", saved.c_str(), 1);
  } else {
    ::unsetenv("PYTHONPATH");
  }

  ASSERT_EQ(state->finalCount(), 1U);
  EXPECT_EQ(state->results[0].status, task::TASK_STATUS_OK);
  EXPECT_EQ(state->results[0].body, "/configured:/inherited");
}

TEST(PyFunctionHandlerTest, CrashDropsOutstandingRemoteCalls) {
  setFakeWorker("remote_then_exit");
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  PyFunctionTaskHandler handler(dispatcher, submitter, makeConfig());

  auto state = std::make_shared<RecordingState>();
  std::unique_ptr<DeadlineTimer> late;
  late = std::make_unique<DeadlineTimer>(dispatcher, [&]() {
    ASSERT_FALSE(submitter.receivers.empty());
    constexpr std::string_view kLateResult = "late-child-result";
    submitter.receivers[0]->Deliver(
        std::as_bytes(std::span(kLateResult.data(), kLateResult.size())), true);
    dispatcher.Shutdown();
  });
  state->on_final = [&]() { late->Schedule(absl::Now() + absl::Milliseconds(100)); };

  handler.HandleTask(makeTask("task-crash-remote", "body"), makeRecordingSender(state));
  dispatcher.Run();

  ASSERT_EQ(state->finalCount(), 1U);
  EXPECT_EQ(state->results[0].status, task::TASK_STATUS_INTERNAL);
  ASSERT_EQ(submitter.tasks.size(), 1U);
}

TEST(PyFunctionHandlerTest, VersionMismatchRejectsWorkerAndFailsTask) {
  setFakeWorker("bad_version");
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);
  FakeChildTaskSubmitter submitter;
  PyFunctionTaskHandler handler(dispatcher, submitter, makeConfig());

  auto state = std::make_shared<RecordingState>();
  state->on_final = [&dispatcher]() { dispatcher.Shutdown(); };
  handler.HandleTask(makeTask("task-bad-version", "body"), makeRecordingSender(state));
  dispatcher.Run();

  ASSERT_EQ(state->finalCount(), 1U);
  EXPECT_EQ(state->results[0].status, task::TASK_STATUS_INTERNAL);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::pyfunction
