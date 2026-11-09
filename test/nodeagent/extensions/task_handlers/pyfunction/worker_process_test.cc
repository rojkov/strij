#include <signal.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "absl/time/time.h"
#include "common/core/event/dispatcher_impl.hh"
#include "gtest/gtest.h"
#include "nodeagent/extensions/task_handlers/pyfunction/deadline_timer.hh"
#include "nodeagent/extensions/task_handlers/pyfunction/worker_process.hh"

namespace strij::nodeagent::task_handlers::pyfunction {
namespace {

using ::strij::extensions::task_handlers::pyfunction::INVOCATION_MODE_PICKLE;

auto fakeWorkerPath() -> std::string {
  const char* srcdir = std::getenv("TEST_SRCDIR");
  const char* workspace = std::getenv("TEST_WORKSPACE");
  if (srcdir == nullptr || workspace == nullptr) {
    return {};
  }
  return std::string(srcdir) + "/" + workspace +
         "/test/nodeagent/extensions/task_handlers/pyfunction/fake_worker";
}

auto makeConfig(const std::string& mode, std::string extra = {}) -> WorkerSpawnConfig {
  WorkerSpawnConfig config;
  config.program_ = fakeWorkerPath();
  config.arguments_ = {mode};
  if (!extra.empty()) {
    config.arguments_.push_back(std::move(extra));
  }
  config.max_frame_bytes_ = 1U << 20U;
  return config;
}

class Watchdog {
public:
  explicit Watchdog(event::Dispatcher& dispatcher)
      : timer_{dispatcher, [&dispatcher]() { dispatcher.Shutdown(); }} {
    timer_.Schedule(absl::Now() + absl::Seconds(10));
  }

private:
  DeadlineTimer timer_;
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(WorkerProcessTest, StartsHandshakesEchoesAndExits) {
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);

  bool ready = false;
  bool result = false;
  bool exited = false;
  std::string body;

  std::unique_ptr<WorkerProcess> worker;
  WorkerCallbacks callbacks;
  callbacks.on_ready_ = [&](std::uint32_t version) {
    EXPECT_EQ(version, kProtocolVersion);
    ready = true;
    WorkerFrame run;
    run.mutable_run()->set_task_id("task-1");
    run.mutable_run()->set_mode(INVOCATION_MODE_PICKLE);
    run.mutable_run()->set_body("payload");
    worker->Send(run);
  };
  callbacks.on_frame_ = [&](const WorkerFrame& frame) {
    if (frame.has_run_result()) {
      result = true;
      body = frame.run_result().body();
      WorkerFrame shutdown;
      shutdown.mutable_shutdown();
      worker->Send(shutdown);
    }
  };
  callbacks.on_exit_ = [&]() {
    exited = true;
    dispatcher.Shutdown();
  };

  worker = std::make_unique<WorkerProcess>(dispatcher, makeConfig("echo"), std::move(callbacks));
  ASSERT_TRUE(worker->Ok());
  worker->Start();
  dispatcher.Run();

  EXPECT_TRUE(ready);
  EXPECT_TRUE(result);
  EXPECT_EQ(body, "payload");
  EXPECT_TRUE(exited);
}

TEST(WorkerProcessTest, WritesFramesLargerThanPipeCapacity) {
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);

  const std::string payload(1U << 20U, 'x');
  bool result = false;
  std::size_t body_size = 0;

  std::unique_ptr<WorkerProcess> worker;
  WorkerCallbacks callbacks;
  callbacks.on_ready_ = [&](std::uint32_t /*version*/) {
    WorkerFrame run;
    run.mutable_run()->set_task_id("task-large");
    run.mutable_run()->set_mode(INVOCATION_MODE_PICKLE);
    run.mutable_run()->set_body(payload);
    worker->Send(run);
  };
  callbacks.on_frame_ = [&](const WorkerFrame& frame) {
    if (frame.has_run_result()) {
      result = true;
      body_size = frame.run_result().body().size();
      WorkerFrame shutdown;
      shutdown.mutable_shutdown();
      worker->Send(shutdown);
    }
  };
  callbacks.on_exit_ = [&]() { dispatcher.Shutdown(); };

  auto config = makeConfig("echo");
  config.max_frame_bytes_ = 4U << 20U;
  worker = std::make_unique<WorkerProcess>(dispatcher, std::move(config), std::move(callbacks));
  ASSERT_TRUE(worker->Ok());
  worker->Start();
  dispatcher.Run();

  EXPECT_TRUE(result);
  EXPECT_EQ(body_size, payload.size());
}

TEST(WorkerProcessTest, ExitsWhileLargeWriteIsPending) {
  // The nodeagent framework ignores SIGPIPE; mirror that here because the test
  // drives WorkerProcess directly and deliberately writes to a dead pipe.
  (void)::signal(SIGPIPE, SIG_IGN);

  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);

  bool exited = false;
  std::unique_ptr<WorkerProcess> worker;
  WorkerCallbacks callbacks;
  callbacks.on_ready_ = [&](std::uint32_t /*version*/) {
    WorkerFrame run;
    run.mutable_run()->set_task_id("task-exit");
    run.mutable_run()->set_mode(INVOCATION_MODE_PICKLE);
    run.mutable_run()->set_body(std::string(1U << 20U, 'x'));
    worker->Send(run);
  };
  callbacks.on_frame_ = [&](const WorkerFrame& /*frame*/) { FAIL() << "unexpected frame"; };
  callbacks.on_exit_ = [&]() {
    exited = true;
    dispatcher.Shutdown();
  };

  auto config = makeConfig("exit_after_ready");
  config.max_frame_bytes_ = 4U << 20U;
  worker = std::make_unique<WorkerProcess>(dispatcher, std::move(config), std::move(callbacks));
  ASSERT_TRUE(worker->Ok());
  worker->Start();
  dispatcher.Run();

  EXPECT_TRUE(exited);
}

TEST(WorkerProcessTest, CapturesStderrWithoutCorruptingProtocol) {
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);

  bool result = false;
  std::string body;

  std::unique_ptr<WorkerProcess> worker;
  WorkerCallbacks callbacks;
  callbacks.on_ready_ = [&](std::uint32_t /*version*/) {
    WorkerFrame run;
    run.mutable_run()->set_task_id("task-2");
    run.mutable_run()->set_mode(INVOCATION_MODE_PICKLE);
    run.mutable_run()->set_body("with-stderr");
    worker->Send(run);
  };
  callbacks.on_frame_ = [&](const WorkerFrame& frame) {
    if (frame.has_run_result()) {
      result = true;
      body = frame.run_result().body();
      dispatcher.Shutdown();
    }
  };
  callbacks.on_exit_ = [&]() { dispatcher.Shutdown(); };

  worker = std::make_unique<WorkerProcess>(dispatcher, makeConfig("stderr"), std::move(callbacks));
  ASSERT_TRUE(worker->Ok());
  worker->Start();
  dispatcher.Run();

  EXPECT_TRUE(result);
  EXPECT_EQ(body, "with-stderr");
}

TEST(WorkerProcessTest, DetectsUnexpectedExitThroughPidfd) {
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);

  bool exited = false;
  bool got_result = false;

  std::unique_ptr<WorkerProcess> worker;
  WorkerCallbacks callbacks;
  callbacks.on_ready_ = [&](std::uint32_t /*version*/) {
    WorkerFrame run;
    run.mutable_run()->set_task_id("task-3");
    run.mutable_run()->set_mode(INVOCATION_MODE_PICKLE);
    worker->Send(run);
  };
  callbacks.on_frame_ = [&](const WorkerFrame& /*frame*/) { got_result = true; };
  callbacks.on_exit_ = [&]() {
    exited = true;
    dispatcher.Shutdown();
  };

  worker =
      std::make_unique<WorkerProcess>(dispatcher, makeConfig("exit_on_run"), std::move(callbacks));
  ASSERT_TRUE(worker->Ok());
  worker->Start();
  dispatcher.Run();

  EXPECT_TRUE(exited);
  EXPECT_FALSE(got_result);
}

TEST(WorkerProcessTest, ReportsWorkerProtocolVersion) {
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);

  std::uint32_t reported = 0;
  std::unique_ptr<WorkerProcess> worker;
  WorkerCallbacks callbacks;
  callbacks.on_ready_ = [&](std::uint32_t version) {
    reported = version;
    worker->Kill();
    dispatcher.Shutdown();
  };
  callbacks.on_exit_ = [&]() { dispatcher.Shutdown(); };

  worker =
      std::make_unique<WorkerProcess>(dispatcher, makeConfig("bad_version"), std::move(callbacks));
  ASSERT_TRUE(worker->Ok());
  worker->Start();
  dispatcher.Run();

  EXPECT_EQ(reported, 999U);
}

TEST(WorkerProcessTest, TerminatesOnOversizedFrame) {
  event::DispatcherImpl dispatcher;
  Watchdog watchdog(dispatcher);

  bool exited = false;
  bool got_frame = false;
  WorkerCallbacks callbacks;
  callbacks.on_ready_ = [&](std::uint32_t /*version*/) {
    FAIL() << "oversized worker became ready";
  };
  callbacks.on_frame_ = [&](const WorkerFrame& /*frame*/) { got_frame = true; };
  callbacks.on_exit_ = [&]() {
    exited = true;
    dispatcher.Shutdown();
  };

  auto worker =
      std::make_unique<WorkerProcess>(dispatcher, makeConfig("oversized"), std::move(callbacks));
  ASSERT_TRUE(worker->Ok());
  worker->Start();
  dispatcher.Run();

  EXPECT_TRUE(exited);
  EXPECT_FALSE(got_frame);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::pyfunction
