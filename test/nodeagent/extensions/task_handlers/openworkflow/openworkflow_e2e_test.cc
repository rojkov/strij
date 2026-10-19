#include <poll.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "test/mocks/event/mocks.hh"
#include "test/nodeagent/extensions/task_handlers/openworkflow/fakes.hh"

#include "absl/strings/str_cat.h"
#include "common/core/utils/task_status.hh"
#include "common/node/capabilities.pb.h"
#include "common/task/task.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nodeagent/core/admission_controller.hh"
#include "nodeagent/core/function_resolver.hh"
#include "nodeagent/core/nodeagent_scheduler_router.hh"
#include "nodeagent/core/run_task_service.hh"
#include "nodeagent/core/task_handler_manager.hh"
#include "nodeagent/extensions/schedulers/default/default_local_scheduler.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/openworkflow_task_handler.hh"
#include "nodeagent/extensions/task_handlers/piped_executable/child_process.hh"
#include "nodeagent/extensions/task_handlers/piped_executable/piped_executable_task_handler.hh"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;

// Completion tags documented in the piped_executable design (private in
// ChildProcess).
constexpr uint8_t kStdinWrite = 0;
constexpr uint8_t kStdoutRead = 1;
constexpr uint8_t kStderrRead = 2;
constexpr uint8_t kExitPoll = 3;

constexpr std::string_view kCallPipedExecutable = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: e2e-call
  version: '0.1.0'
input:
  from: '${ "hello" }'
do:
  - run:
      call: strij
      with:
        type: piped_executable
        parameters:
          function: /bin/cat
        requirements:
          cpu: 1
)YAML";

constexpr std::string_view kCallGhost = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: e2e-refused
  version: '0.1.0'
do:
  - run:
      call: strij
      with:
        type: ghost
)YAML";

auto MakeParent(const std::string& id, const std::string& function, std::string body)
    -> task::Task {
  task::Task task;
  task.set_id(id);
  task.set_type("openworkflow");
  (*task.mutable_parameters())[std::string(kFunctionParameter)] = function;
  task.set_body(std::move(body));
  return task;
}

auto WaitForExit(int pidfd) -> bool {
  struct pollfd pfd{};
  pfd.fd = pidfd;
  pfd.events = POLLIN;
  return ::poll(&pfd, 1, 5000) > 0;
}

// Locates `scripts/openworkflow_echo.sh` in the test's runfiles (declared as a
// `data` dependency of this test target).
auto FindEchoScript() -> std::string {
  const char* srcdir = std::getenv("TEST_SRCDIR");
  if (srcdir == nullptr) {
    return {};
  }
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iter(srcdir, error), end;
       iter != end && !error; iter.increment(error)) {
    if (iter->is_regular_file() && iter->path().filename() == "openworkflow_echo.sh") {
      return iter->path().string();
    }
  }
  return {};
}

class OpenWorkflowE2ETest : public ::testing::Test {
protected:
  void SetUp() override {
    auto caps = std::make_shared<node::NodeCapabilities>();
    caps->set_node_id("node-e2e");
    caps->set_capability_version(1);
    auto* pool = caps->add_pools();
    pool->set_name("cpu");
    pool->set_total(16);

    dispatcher_ = std::make_shared<NiceMock<event::MockDispatcher>>();
    admission_ = std::make_shared<AdmissionControllerImpl>(*caps, *dispatcher_);

    manager_ = std::make_shared<TaskHandlerManager>();
    run_task_service_ = std::make_unique<RunTaskServiceImpl>(manager_, admission_);

    auto default_scheduler = std::make_unique<nodeagent::schedulers::DefaultLocalScheduler>(
        *run_task_service_, admission_, nullptr);
    std::vector<NodeagentSchedulerRouter::ChildRoutedScheduler> routed;
    routed.push_back(
        {.scheduler = std::move(default_scheduler), .task_type = "", .local_default = true});
    router_ = std::make_unique<NodeagentSchedulerRouter>(std::move(routed));

    manager_->AddHandler("piped_executable",
                         std::make_unique<PipedExecutableTaskHandler>(*dispatcher_, resolver_));

    static std::uint64_t counter = 0;
    dir_ =
        std::filesystem::temp_directory_path() /
        ("strij-openworkflow-e2e-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    std::filesystem::create_directories(dir_);
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }

  auto WriteDocument(std::string_view name, std::string_view content) -> std::string {
    const std::filesystem::path path = dir_ / name;
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();

    return path.string();
  }

  std::shared_ptr<NiceMock<event::MockDispatcher>> dispatcher_;
  std::shared_ptr<AdmissionController> admission_;
  std::shared_ptr<TaskHandlerManager> manager_;
  std::unique_ptr<RunTaskServiceImpl> run_task_service_;
  std::unique_ptr<NodeagentSchedulerRouter> router_;
  LocalFunctionResolver resolver_;
  std::filesystem::path dir_;
};

TEST_F(OpenWorkflowE2ETest, ChildOutputBecomesWorkflowFinalResult) {
  OpenWorkflowTaskHandler handler(*router_, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  int stdin_w = -1;
  int pidfd = -1;
  event::Completable* child_raw = nullptr;
  EXPECT_CALL(*dispatcher_, PrepareWrite(_, kStdinWrite, _, _, -1))
      .WillOnce(DoAll(SaveArg<2>(&stdin_w), Return()));
  EXPECT_CALL(*dispatcher_, PreparePoll(_, kExitPoll, _, POLLIN))
      .WillOnce(DoAll(SaveArg<0>(&child_raw), SaveArg<2>(&pidfd), Return()));

  const std::string document = WriteDocument("call-piped.yaml", kCallPipedExecutable);
  handler.HandleTask(MakeParent("wf", document, ""), std::make_unique<test::TestSender>(state));

  ASSERT_NE(child_raw, nullptr);
  ASSERT_GE(stdin_w, 0);
  ASSERT_GE(pidfd, 0);
  EXPECT_TRUE(state->sent_.empty());

  auto* child = static_cast<ChildProcess*>(child_raw);
  ASSERT_EQ(::write(stdin_w, "hello", 5), 5);
  child->HandleCompletion(kStdinWrite, 5, 0);
  ASSERT_TRUE(WaitForExit(pidfd));

  event::Command deferred{};
  EXPECT_CALL(*dispatcher_, SubmitCommand(_)).WillOnce(SaveArg<0>(&deferred));

  child->HandleCompletion(kExitPoll, POLLIN, 0);
  child->HandleCompletion(kStdoutRead, 0, 0);
  child->HandleCompletion(kStderrRead, 0, 0);

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].id(), "wf");
  EXPECT_EQ(state->sent_[0].body(), "hello");
  EXPECT_TRUE(state->sent_[0].is_final());
  EXPECT_TRUE(utils::IsTaskResultOk(state->sent_[0]));

  auto* piped = dynamic_cast<PipedExecutableTaskHandler*>(manager_->GetHandler("piped_executable"));
  ASSERT_NE(piped, nullptr);
  piped->ProcessCommand(deferred);
}

// The documented manual scenario, exercised through the real piped_executable
// handler: a two-step `call: strij` workflow whose steps run
// scripts/openworkflow_echo.sh, with each child's output (the data chain)
// feeding the next. Drives two sequential child processes.
TEST_F(OpenWorkflowE2ETest, ScriptedTwoStepWorkflowChainsChildOutputs) {
  const std::string script = FindEchoScript();
  ASSERT_FALSE(script.empty()) << "scripts/openworkflow_echo.sh not found in runfiles";

  const std::string document_text = absl::StrCat("document:\n"
                                                 "  dsl: '1.0.3'\n"
                                                 "  namespace: test\n"
                                                 "  name: e2e-script\n"
                                                 "  version: '0.1.0'\n"
                                                 "do:\n"
                                                 "  - first:\n"
                                                 "      call: strij\n"
                                                 "      input:\n"
                                                 "        from: ${ . }\n"
                                                 "      with:\n"
                                                 "        type: piped_executable\n"
                                                 "        parameters:\n"
                                                 "          function: ",
                                                 script,
                                                 "\n"
                                                 "        requirements:\n"
                                                 "          cpu: 1\n"
                                                 "  - second:\n"
                                                 "      call: strij\n"
                                                 "      with:\n"
                                                 "        type: piped_executable\n"
                                                 "        parameters:\n"
                                                 "          function: ",
                                                 script,
                                                 "\n"
                                                 "        requirements:\n"
                                                 "          cpu: 1\n");

  OpenWorkflowTaskHandler handler(*router_, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  int stdin_w1 = -1;
  int stdin_w2 = -1;
  int pidfd1 = -1;
  int pidfd2 = -1;
  event::Completable* child1_raw = nullptr;
  event::Completable* child2_raw = nullptr;
  EXPECT_CALL(*dispatcher_, PrepareWrite(_, kStdinWrite, _, _, -1))
      .WillOnce(DoAll(SaveArg<2>(&stdin_w1), Return()))
      .WillOnce(DoAll(SaveArg<2>(&stdin_w2), Return()));
  EXPECT_CALL(*dispatcher_, PreparePoll(_, kExitPoll, _, POLLIN))
      .WillOnce(DoAll(SaveArg<0>(&child1_raw), SaveArg<2>(&pidfd1), Return()))
      .WillOnce(DoAll(SaveArg<0>(&child2_raw), SaveArg<2>(&pidfd2), Return()));
  event::Command deferred1{};
  event::Command deferred2{};
  EXPECT_CALL(*dispatcher_, SubmitCommand(_))
      .WillOnce(SaveArg<0>(&deferred1))
      .WillOnce(SaveArg<0>(&deferred2));

  const std::string document = WriteDocument("scripted.yaml", document_text);
  handler.HandleTask(MakeParent("wf", document, R"({"name":"world"})"),
                     std::make_unique<test::TestSender>(state));

  // Step one: its input is the seeded data chain.
  const std::string body1 = R"({"name":"world"})";
  ASSERT_NE(child1_raw, nullptr);
  ASSERT_GE(stdin_w1, 0);
  auto* child1 = static_cast<ChildProcess*>(child1_raw);
  ASSERT_EQ(::write(stdin_w1, body1.data(), body1.size()), static_cast<ssize_t>(body1.size()));
  child1->HandleCompletion(kStdinWrite, static_cast<int>(body1.size()), 0);
  ASSERT_TRUE(WaitForExit(pidfd1));

  // Completing step one resumes the workflow, which spawns the second child.
  child1->HandleCompletion(kExitPoll, POLLIN, 0);
  child1->HandleCompletion(kStdoutRead, 0, 0);
  child1->HandleCompletion(kStderrRead, 0, 0);
  EXPECT_TRUE(state->sent_.empty());

  // Step two: its input is step one's output, read back as the data chain.
  const std::string body2 = R"({"echo":{"name":"world"}})";
  ASSERT_NE(child2_raw, nullptr);
  ASSERT_GE(stdin_w2, 0);
  auto* child2 = static_cast<ChildProcess*>(child2_raw);
  ASSERT_EQ(::write(stdin_w2, body2.data(), body2.size()), static_cast<ssize_t>(body2.size()));
  child2->HandleCompletion(kStdinWrite, static_cast<int>(body2.size()), 0);
  ASSERT_TRUE(WaitForExit(pidfd2));

  child2->HandleCompletion(kExitPoll, POLLIN, 0);
  child2->HandleCompletion(kStdoutRead, 0, 0);
  child2->HandleCompletion(kStderrRead, 0, 0);

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].id(), "wf");
  EXPECT_EQ(state->sent_[0].body(), R"({"echo":{"echo":{"name":"world"}}})");
  EXPECT_TRUE(state->sent_[0].is_final());
  EXPECT_TRUE(utils::IsTaskResultOk(state->sent_[0]));

  auto* piped = dynamic_cast<PipedExecutableTaskHandler*>(manager_->GetHandler("piped_executable"));
  ASSERT_NE(piped, nullptr);
  piped->ProcessCommand(deferred1);
  piped->ProcessCommand(deferred2);
}

TEST_F(OpenWorkflowE2ETest, ForkJoinAllRunsBranchesAndJoinsInDeclarationOrder) {
  const std::string script = FindEchoScript();
  ASSERT_FALSE(script.empty()) << "scripts/openworkflow_echo.sh not found in runfiles";

  const std::string document_text = absl::StrCat("document:\n"
                                                 "  dsl: '1.0.3'\n"
                                                 "  namespace: test\n"
                                                 "  name: e2e-fork-all\n"
                                                 "  version: '0.1.0'\n"
                                                 "do:\n"
                                                 "  - both:\n"
                                                 "      fork:\n"
                                                 "        branches:\n"
                                                 "          - first:\n"
                                                 "              call: strij\n"
                                                 "              input:\n"
                                                 "                from: ${ {\"branch\":1} }\n"
                                                 "              with:\n"
                                                 "                type: piped_executable\n"
                                                 "                parameters:\n"
                                                 "                  function: ",
                                                 script,
                                                 "\n"
                                                 "                requirements:\n"
                                                 "                  cpu: 1\n"
                                                 "          - second:\n"
                                                 "              call: strij\n"
                                                 "              input:\n"
                                                 "                from: ${ {\"branch\":2} }\n"
                                                 "              with:\n"
                                                 "                type: piped_executable\n"
                                                 "                parameters:\n"
                                                 "                  function: ",
                                                 script,
                                                 "\n"
                                                 "                requirements:\n"
                                                 "                  cpu: 1\n"
                                                 "  - after:\n"
                                                 "      call: strij\n"
                                                 "      input:\n"
                                                 "        from: ${ . }\n"
                                                 "      with:\n"
                                                 "        type: piped_executable\n"
                                                 "        parameters:\n"
                                                 "          function: ",
                                                 script,
                                                 "\n"
                                                 "        requirements:\n"
                                                 "          cpu: 1\n");

  OpenWorkflowTaskHandler handler(*router_, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  int stdin1 = -1;
  int stdin2 = -1;
  int stdin3 = -1;
  int pidfd1 = -1;
  int pidfd2 = -1;
  int pidfd3 = -1;
  event::Completable* child1_raw = nullptr;
  event::Completable* child2_raw = nullptr;
  event::Completable* child3_raw = nullptr;
  EXPECT_CALL(*dispatcher_, PrepareWrite(_, kStdinWrite, _, _, -1))
      .WillOnce(DoAll(SaveArg<2>(&stdin1), Return()))
      .WillOnce(DoAll(SaveArg<2>(&stdin2), Return()))
      .WillOnce(DoAll(SaveArg<2>(&stdin3), Return()));
  EXPECT_CALL(*dispatcher_, PreparePoll(_, kExitPoll, _, POLLIN))
      .WillOnce(DoAll(SaveArg<0>(&child1_raw), SaveArg<2>(&pidfd1), Return()))
      .WillOnce(DoAll(SaveArg<0>(&child2_raw), SaveArg<2>(&pidfd2), Return()))
      .WillOnce(DoAll(SaveArg<0>(&child3_raw), SaveArg<2>(&pidfd3), Return()));

  const std::string document = WriteDocument("fork-all.yaml", document_text);
  handler.HandleTask(MakeParent("wf", document, ""), std::make_unique<test::TestSender>(state));

  auto complete = [](event::Completable* raw, int stdin_fd, int pidfd, std::string_view body) {
    ASSERT_NE(raw, nullptr);
    ASSERT_GE(stdin_fd, 0);
    ASSERT_EQ(::write(stdin_fd, body.data(), body.size()), static_cast<ssize_t>(body.size()));
    auto* child = static_cast<ChildProcess*>(raw);
    child->HandleCompletion(kStdinWrite, static_cast<int>(body.size()), 0);
    ASSERT_TRUE(WaitForExit(pidfd));
    child->HandleCompletion(kExitPoll, POLLIN, 0);
    child->HandleCompletion(kStdoutRead, 0, 0);
    child->HandleCompletion(kStderrRead, 0, 0);
  };

  complete(child1_raw, stdin1, pidfd1, R"({"branch":1})");
  EXPECT_TRUE(state->sent_.empty());
  ASSERT_NE(child2_raw, nullptr);

  complete(child2_raw, stdin2, pidfd2, R"({"branch":2})");
  EXPECT_TRUE(state->sent_.empty());

  // Both branches have joined; the next step's input is the ordered array.
  ASSERT_NE(child3_raw, nullptr);
  const std::string joined = R"([{"echo":{"branch":1}},{"echo":{"branch":2}}])";
  complete(child3_raw, stdin3, pidfd3, joined);

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].id(), "wf");
  EXPECT_EQ(state->sent_[0].body(), R"({"echo":[{"echo":{"branch":1}},{"echo":{"branch":2}}]})");
  EXPECT_TRUE(state->sent_[0].is_final());
  EXPECT_TRUE(utils::IsTaskResultOk(state->sent_[0]));
}

TEST_F(OpenWorkflowE2ETest, ForkCompeteResolvesOnFirstBranchAndIgnoresTheLoser) {
  const std::string script = FindEchoScript();
  ASSERT_FALSE(script.empty()) << "scripts/openworkflow_echo.sh not found in runfiles";

  const std::string document_text = absl::StrCat("document:\n"
                                                 "  dsl: '1.0.3'\n"
                                                 "  namespace: test\n"
                                                 "  name: e2e-fork-compete\n"
                                                 "  version: '0.1.0'\n"
                                                 "do:\n"
                                                 "  - race:\n"
                                                 "      fork:\n"
                                                 "        compete: true\n"
                                                 "        branches:\n"
                                                 "          - first:\n"
                                                 "              call: strij\n"
                                                 "              input:\n"
                                                 "                from: ${ {\"branch\":1} }\n"
                                                 "              with:\n"
                                                 "                type: piped_executable\n"
                                                 "                parameters:\n"
                                                 "                  function: ",
                                                 script,
                                                 "\n"
                                                 "                requirements:\n"
                                                 "                  cpu: 1\n"
                                                 "          - second:\n"
                                                 "              call: strij\n"
                                                 "              input:\n"
                                                 "                from: ${ {\"branch\":2} }\n"
                                                 "              with:\n"
                                                 "                type: piped_executable\n"
                                                 "                parameters:\n"
                                                 "                  function: ",
                                                 script,
                                                 "\n"
                                                 "                requirements:\n"
                                                 "                  cpu: 1\n"
                                                 "  - after:\n"
                                                 "      call: strij\n"
                                                 "      input:\n"
                                                 "        from: ${ . }\n"
                                                 "      with:\n"
                                                 "        type: piped_executable\n"
                                                 "        parameters:\n"
                                                 "          function: ",
                                                 script,
                                                 "\n"
                                                 "        requirements:\n"
                                                 "          cpu: 1\n");

  OpenWorkflowTaskHandler handler(*router_, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  int stdin1 = -1;
  int stdin2 = -1;
  int stdin3 = -1;
  int pidfd1 = -1;
  int pidfd2 = -1;
  int pidfd3 = -1;
  event::Completable* child1_raw = nullptr;
  event::Completable* child2_raw = nullptr;
  event::Completable* child3_raw = nullptr;
  EXPECT_CALL(*dispatcher_, PrepareWrite(_, kStdinWrite, _, _, -1))
      .WillOnce(DoAll(SaveArg<2>(&stdin1), Return()))
      .WillOnce(DoAll(SaveArg<2>(&stdin2), Return()))
      .WillOnce(DoAll(SaveArg<2>(&stdin3), Return()));
  EXPECT_CALL(*dispatcher_, PreparePoll(_, kExitPoll, _, POLLIN))
      .WillOnce(DoAll(SaveArg<0>(&child1_raw), SaveArg<2>(&pidfd1), Return()))
      .WillOnce(DoAll(SaveArg<0>(&child2_raw), SaveArg<2>(&pidfd2), Return()))
      .WillOnce(DoAll(SaveArg<0>(&child3_raw), SaveArg<2>(&pidfd3), Return()));

  const std::string document = WriteDocument("fork-compete.yaml", document_text);
  handler.HandleTask(MakeParent("wf", document, ""), std::make_unique<test::TestSender>(state));

  auto complete = [](event::Completable* raw, int stdin_fd, int pidfd, std::string_view body) {
    ASSERT_NE(raw, nullptr);
    ASSERT_GE(stdin_fd, 0);
    ASSERT_EQ(::write(stdin_fd, body.data(), body.size()), static_cast<ssize_t>(body.size()));
    auto* child = static_cast<ChildProcess*>(raw);
    child->HandleCompletion(kStdinWrite, static_cast<int>(body.size()), 0);
    ASSERT_TRUE(WaitForExit(pidfd));
    child->HandleCompletion(kExitPoll, POLLIN, 0);
    child->HandleCompletion(kStdoutRead, 0, 0);
    child->HandleCompletion(kStderrRead, 0, 0);
  };

  // The second branch wins the race; the fork resolves without waiting for
  // the first.
  complete(child2_raw, stdin2, pidfd2, R"({"branch":2})");
  EXPECT_TRUE(state->sent_.empty());
  ASSERT_NE(child3_raw, nullptr);

  // The loser completes afterwards; its outcome is ignored.
  complete(child1_raw, stdin1, pidfd1, R"({"branch":1})");
  EXPECT_TRUE(state->sent_.empty());

  // The step after the fork received the winner's result.
  complete(child3_raw, stdin3, pidfd3, R"({"echo":{"branch":2}})");

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].body(), R"({"echo":{"echo":{"branch":2}}})");
  EXPECT_TRUE(utils::IsTaskResultOk(state->sent_[0]));
}

TEST_F(OpenWorkflowE2ETest, RefusedChildSurfacesAsWorkflowFailureNamingStep) {
  OpenWorkflowTaskHandler handler(*router_, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  const std::string document = WriteDocument("call-ghost.yaml", kCallGhost);
  handler.HandleTask(MakeParent("wf", document, ""), std::make_unique<test::TestSender>(state));

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].id(), "wf");
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_CAPACITY_REFUSED);
  EXPECT_NE(state->sent_[0].body().find("run"), std::string::npos);
  EXPECT_TRUE(state->sent_[0].is_final());
  EXPECT_FALSE(utils::IsTaskResultOk(state->sent_[0]));
}

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
