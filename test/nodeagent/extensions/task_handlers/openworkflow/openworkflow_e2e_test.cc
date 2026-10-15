#include <poll.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
#include "test/mocks/event/mocks.hh"
#include "test/nodeagent/extensions/task_handlers/openworkflow/fakes.hh"

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

auto MakeParent(const std::string& id, std::string body) -> task::Task {
  task::Task task;
  task.set_id(id);
  task.set_type("openworkflow");
  task.set_body(std::move(body));
  return task;
}

auto WaitForExit(int pidfd) -> bool {
  struct pollfd pfd{};
  pfd.fd = pidfd;
  pfd.events = POLLIN;
  return ::poll(&pfd, 1, 5000) > 0;
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
  }

  std::shared_ptr<NiceMock<event::MockDispatcher>> dispatcher_;
  std::shared_ptr<AdmissionController> admission_;
  std::shared_ptr<TaskHandlerManager> manager_;
  std::unique_ptr<RunTaskServiceImpl> run_task_service_;
  std::unique_ptr<NodeagentSchedulerRouter> router_;
  LocalFunctionResolver resolver_;
};

TEST_F(OpenWorkflowE2ETest, ChildOutputBecomesWorkflowFinalResult) {
  OpenWorkflowTaskHandler handler(*router_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  int stdin_w = -1;
  int pidfd = -1;
  event::Completable* child_raw = nullptr;
  EXPECT_CALL(*dispatcher_, PrepareWrite(_, kStdinWrite, _, _, -1))
      .WillOnce(DoAll(SaveArg<2>(&stdin_w), Return()));
  EXPECT_CALL(*dispatcher_, PreparePoll(_, kExitPoll, _, POLLIN))
      .WillOnce(DoAll(SaveArg<0>(&child_raw), SaveArg<2>(&pidfd), Return()));

  handler.HandleTask(MakeParent("wf", std::string(kCallPipedExecutable)),
                     std::make_unique<test::TestSender>(state));

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

TEST_F(OpenWorkflowE2ETest, RefusedChildSurfacesAsWorkflowFailureNamingStep) {
  OpenWorkflowTaskHandler handler(*router_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  handler.HandleTask(MakeParent("wf", std::string(kCallGhost)),
                     std::make_unique<test::TestSender>(state));

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].id(), "wf");
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_CAPACITY_REFUSED);
  EXPECT_NE(state->sent_[0].body().find("run"), std::string::npos);
  EXPECT_TRUE(state->sent_[0].is_final());
  EXPECT_FALSE(utils::IsTaskResultOk(state->sent_[0]));
}

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
