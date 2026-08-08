#include "extensions/task_handlers/piped_executable/piped_executable_task_handler.hh"

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/extensions/function_resolver.hh"
#include "core/task/task.pb.h"
#include "extensions/task_handlers/piped_executable/child_process.hh"
#include "extensions/task_handlers/piped_executable/piped_executable.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/mocks/event/mocks.hh"
#include "test/mocks/extensions/extensions_mocks.hh"

namespace strij::extensions::task_handlers {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;

// Tags documented in the design: ChildProcess keeps them private.
constexpr uint8_t kStdinWrite = 0;
constexpr uint8_t kStdoutRead = 1;
constexpr uint8_t kStderrRead = 2;
constexpr uint8_t kExitPoll = 3;

// Test ResultSender sharing state across the sender instances the handler
// owns per task.
struct SenderState {
  std::vector<strij::task::TaskResult> sent;
  std::move_only_function<void()> close_cb;
};

class TestSender final : public ResultSender {
public:
  explicit TestSender(std::shared_ptr<SenderState> state) : state_{std::move(state)} {}

  void Send(strij::task::TaskResult result) override { state_->sent.push_back(std::move(result)); }

  auto RegisterOnClose(std::move_only_function<void()> close_cb) -> std::size_t override {
    state_->close_cb = std::move(close_cb);
    return ++next_token_;
  }

  void UnregisterOnClose(std::size_t /*token*/) override { state_->close_cb = nullptr; }

private:
  std::shared_ptr<SenderState> state_;
  std::size_t next_token_{0};
};

auto MakeTask(const std::string& id, const std::string& function, const std::string& body)
    -> strij::task::Task {
  strij::task::Task task;
  task.set_id(id);
  task.set_type("piped_executable");
  task.set_body(body);
  task.mutable_parameters()->insert(
      {std::string(strij::extensions::kFunctionParameter), function});
  return task;
}

// Waits until the process behind `pidfd` has exited.
auto WaitForExit(int pidfd) -> bool {
  struct pollfd pfd {};
  pfd.fd = pidfd;
  pfd.events = POLLIN;
  return ::poll(&pfd, 1, 5000) > 0;
}

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(PipedExecutableTaskHandlerFactoryTest, NameIsPipedExecutable) {
  PipedExecutableTaskHandlerFactory factory;
  EXPECT_EQ(factory.Name(), "piped_executable");
}

TEST(PipedExecutableTaskHandlerFactoryTest, CreateUsesSharedResolverAndDispatcher) {
  NiceMock<strij::event::MockDispatcher> dispatcher;
  strij::extensions::LocalFunctionResolver resolver;
  strij::extensions::MockFactoryContext context;
  ON_CALL(context, Dispatcher()).WillByDefault(ReturnRef(dispatcher));
  ON_CALL(context, FunctionResolver()).WillByDefault(ReturnRef(resolver));

  strij::extensions::task_handlers::piped_executable::PipedExecutableTaskHandlerConfig config;
  auto handler = PipedExecutableTaskHandlerFactory().Create(config, context);

  ASSERT_NE(handler, nullptr);
  // The handler is wired to the shared resolver and dispatcher: a task with a
  // resolvable function must spawn a child, observable as the armed exit poll.
  EXPECT_CALL(dispatcher, PreparePoll(_, kExitPoll, _, POLLIN)).WillOnce(Return());
  auto task = MakeTask("1", "/bin/cat", "x");
  handler->HandleTask(task, std::make_unique<TestSender>(std::make_shared<SenderState>()));
}

TEST(PipedExecutableTaskHandlerTest, MissingFunctionParameterSendsEmptyFinal) {
  NiceMock<strij::event::MockDispatcher> dispatcher;
  strij::extensions::LocalFunctionResolver resolver;
  PipedExecutableTaskHandler handler(dispatcher, resolver);

  strij::task::Task task = MakeTask("42", "", "");
  auto state = std::make_shared<SenderState>();

  handler.HandleTask(task, std::make_unique<TestSender>(state));

  ASSERT_EQ(state->sent.size(), 1u);
  EXPECT_EQ(state->sent[0].id(), "42");
  EXPECT_TRUE(state->sent[0].body().empty());
  EXPECT_TRUE(state->sent[0].is_final());
}

TEST(PipedExecutableTaskHandlerTest, SpawnFailureSendsEmptyFinal) {
  NiceMock<strij::event::MockDispatcher> dispatcher;
  strij::extensions::LocalFunctionResolver resolver;
  PipedExecutableTaskHandler handler(dispatcher, resolver);

  auto task = MakeTask("7", "/nonexistent/strij-binary", "payload");
  auto state = std::make_shared<SenderState>();

  handler.HandleTask(task, std::make_unique<TestSender>(state));

  ASSERT_EQ(state->sent.size(), 1u);
  EXPECT_EQ(state->sent[0].id(), "7");
  EXPECT_TRUE(state->sent[0].body().empty());
  EXPECT_TRUE(state->sent[0].is_final());
}

TEST(PipedExecutableTaskHandlerTest, HandleTaskSpawnsStreamsAndTeardowns) {
  NiceMock<strij::event::MockDispatcher> dispatcher;
  strij::extensions::LocalFunctionResolver resolver;
  PipedExecutableTaskHandler handler(dispatcher, resolver);

  int stdin_w = -1;
  int pidfd = -1;
  strij::event::Completable* child_raw = nullptr;
  EXPECT_CALL(dispatcher, PrepareWrite(_, _, _, _, -1))
      .WillOnce(DoAll(SaveArg<2>(&stdin_w), Return()));
  EXPECT_CALL(dispatcher, PreparePoll(_, kExitPoll, _, POLLIN))
      .WillOnce(DoAll(SaveArg<0>(&child_raw), SaveArg<2>(&pidfd), Return()));

  auto task = MakeTask("42", "/bin/cat", "hello");
  auto state = std::make_shared<SenderState>();
  handler.HandleTask(task, std::make_unique<TestSender>(state));

  ASSERT_NE(child_raw, nullptr);
  ASSERT_GE(stdin_w, 0);
  ASSERT_GE(pidfd, 0);
  ASSERT_EQ(state->sent.size(), 0u);

  auto* child = static_cast<ChildProcess*>(child_raw);

  // Feed the child's stdin for real, then complete the stdin write.
  ASSERT_EQ(::write(stdin_w, "hello", 5), 5);
  child->HandleCompletion(kStdinWrite, 5, 0);

  // /bin/cat echoes "hello" and exits; wait for the real exit before driving
  // the exit poll so the drain sees the data.
  ASSERT_TRUE(WaitForExit(pidfd));

  strij::event::Command deferred{};
  EXPECT_CALL(dispatcher, SubmitCommand(_)).WillOnce(SaveArg<0>(&deferred));

  child->HandleCompletion(kExitPoll, POLLIN, 0);
  child->HandleCompletion(kStdoutRead, 0, 0);
  child->HandleCompletion(kStderrRead, 0, 0);

  ASSERT_EQ(state->sent.size(), 1u);
  EXPECT_EQ(state->sent[0].id(), "42");
  EXPECT_EQ(state->sent[0].body(), "hello");
  EXPECT_TRUE(state->sent[0].is_final());

  // Teardown: the close callback was unregistered and a DEFERRED_DELETE
  // command was submitted to the handler, which erases the map entry.
  EXPECT_EQ(state->close_cb, nullptr);
  EXPECT_EQ(deferred.type_, strij::event::Command::DEFERRED_DELETE);
  EXPECT_EQ(deferred.destination_, static_cast<strij::event::CommandHandler*>(&handler));
  EXPECT_EQ(deferred.args_, child_raw);
  handler.ProcessCommand(deferred);
}

TEST(PipedExecutableTaskHandlerTest, StreamsStdoutAsNonFinalChunks) {
  NiceMock<strij::event::MockDispatcher> dispatcher;
  strij::extensions::LocalFunctionResolver resolver;
  PipedExecutableTaskHandler handler(dispatcher, resolver);

  int pidfd = -1;
  int stdin_w = -1;
  strij::event::Completable* child_raw = nullptr;
  EXPECT_CALL(dispatcher, PrepareWrite(_, _, _, _, -1))
      .WillOnce(DoAll(SaveArg<2>(&stdin_w), Return()));
  EXPECT_CALL(dispatcher, PreparePoll(_, kExitPoll, _, POLLIN))
      .WillOnce(DoAll(SaveArg<0>(&child_raw), SaveArg<2>(&pidfd), Return()));
  // The first kStdoutRead re-arms after delivering a chunk.
  EXPECT_CALL(dispatcher, PrepareRead(_, kStdoutRead, _, _, -1)).WillRepeatedly(Return());
  EXPECT_CALL(dispatcher, PrepareRead(_, kStderrRead, _, _, -1)).WillRepeatedly(Return());

  auto task = MakeTask("11", "/bin/cat", "first-second");
  auto state = std::make_shared<SenderState>();
  handler.HandleTask(task, std::make_unique<TestSender>(state));

  auto* child = static_cast<ChildProcess*>(child_raw);

  // Two partial reads, as if the dispatcher delivered fragmented output.
  child->HandleCompletion(kStdoutRead, 5, 0);
  ASSERT_EQ(state->sent.size(), 1u);
  EXPECT_EQ(state->sent[0].id(), "11");
  EXPECT_EQ(state->sent[0].body().size(), 5u);
  // Streaming chunks carry an explicit is_final=false; absence means final.
  EXPECT_TRUE(state->sent[0].has_is_final());
  EXPECT_FALSE(state->sent[0].is_final());

  child->HandleCompletion(kStdoutRead, 6, 0);
  ASSERT_EQ(state->sent.size(), 2u);
  EXPECT_EQ(state->sent[1].body().size(), 6u);
  EXPECT_TRUE(state->sent[1].has_is_final());
  EXPECT_FALSE(state->sent[1].is_final());

  // Complete the stdin write so the pipe write end closes and /bin/cat exits.
  child->HandleCompletion(kStdinWrite, 12, 0);
  ASSERT_TRUE(WaitForExit(pidfd));

  strij::event::Command deferred{};
  EXPECT_CALL(dispatcher, SubmitCommand(_)).WillOnce(SaveArg<0>(&deferred));

  child->HandleCompletion(kStdoutRead, 0, 0);
  child->HandleCompletion(kStderrRead, 0, 0);
  child->HandleCompletion(kExitPoll, POLLIN, 0);

  // Empty stdout after the drain -> a single final empty result.
  ASSERT_EQ(state->sent.size(), 3u);
  EXPECT_EQ(state->sent[2].id(), "11");
  EXPECT_TRUE(state->sent[2].body().empty());
  EXPECT_TRUE(state->sent[2].is_final());
  EXPECT_EQ(state->close_cb, nullptr);
  handler.ProcessCommand(deferred);
  (void)stdin_w;
}

TEST(PipedExecutableTaskHandlerTest, ConnectionCloseKillsChild) {
  NiceMock<strij::event::MockDispatcher> dispatcher;
  strij::extensions::LocalFunctionResolver resolver;
  PipedExecutableTaskHandler handler(dispatcher, resolver);

  int pidfd = -1;
  strij::event::Completable* child_raw = nullptr;
  EXPECT_CALL(dispatcher, PreparePoll(_, kExitPoll, _, POLLIN))
      .WillOnce(DoAll(SaveArg<0>(&child_raw), SaveArg<2>(&pidfd), Return()));

  auto task = MakeTask("3", "/bin/cat", "long-lived");
  auto state = std::make_shared<SenderState>();
  handler.HandleTask(task, std::make_unique<TestSender>(state));

  auto* child = static_cast<ChildProcess*>(child_raw);

  // Complete the pending stdin write so teardown can proceed.
  child->HandleCompletion(kStdinWrite, 10, 0);

  // Connection teardown fires the close callback: the child must be killed.
  ASSERT_NE(state->close_cb, nullptr);
  state->close_cb();

  EXPECT_TRUE(WaitForExit(pidfd));

  strij::event::Command deferred{};
  EXPECT_CALL(dispatcher, SubmitCommand(_)).WillOnce(SaveArg<0>(&deferred));

  child->HandleCompletion(kExitPoll, POLLIN, 0);
  child->HandleCompletion(kStdoutRead, 0, 0);
  child->HandleCompletion(kStderrRead, 0, 0);

  ASSERT_EQ(state->sent.size(), 1u);
  EXPECT_EQ(state->sent[0].id(), "3");
  EXPECT_TRUE(state->sent[0].is_final());
  EXPECT_EQ(state->close_cb, nullptr);
  handler.ProcessCommand(deferred);
}

TEST(PipedExecutableTaskHandlerTest, ConcurrentTasksAreIsolated) {
  NiceMock<strij::event::MockDispatcher> dispatcher;
  strij::extensions::LocalFunctionResolver resolver;
  PipedExecutableTaskHandler handler(dispatcher, resolver);

  int stdin_w_a = -1;
  int stdin_w_b = -1;
  strij::event::Completable* child_a = nullptr;
  strij::event::Completable* child_b = nullptr;
  EXPECT_CALL(dispatcher, PrepareWrite(_, _, _, _, -1))
      .WillOnce(DoAll(SaveArg<2>(&stdin_w_a), Return()))
      .WillOnce(DoAll(SaveArg<2>(&stdin_w_b), Return()));
  EXPECT_CALL(dispatcher, PreparePoll(_, kExitPoll, _, POLLIN))
      .WillOnce(DoAll(SaveArg<0>(&child_a), Return()))
      .WillOnce(DoAll(SaveArg<0>(&child_b), Return()));

  auto state = std::make_shared<SenderState>();

  handler.HandleTask(MakeTask("a", "/bin/cat", "x"), std::make_unique<TestSender>(state));
  handler.HandleTask(MakeTask("b", "/bin/cat", "y"), std::make_unique<TestSender>(state));

  EXPECT_NE(child_a, nullptr);
  EXPECT_NE(child_b, nullptr);
  EXPECT_NE(child_a, child_b);
  EXPECT_NE(stdin_w_a, stdin_w_b);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::extensions::task_handlers
