#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "common/node/capabilities.pb.h"
#include "common/task/task.pb.h"
#include "nodeagent/core/admission_controller.hh"
#include "nodeagent/core/child_submission_service.hh"
#include "nodeagent/core/local_receiver_registry.hh"
#include "nodeagent/core/registry_result_sender.hh"
#include "nodeagent/core/run_task_service.hh"
#include "nodeagent/core/task_handler_manager.hh"
#include "nodeagent/extensions/schedulers/push/push_local_scheduler.hh"
#include "nodeagent/extensions/task_handlers/echo/echo_task_handler.hh"
#include "strij/gateway/result_receiver_storage.hh"
#include "test/mocks/event/mocks.hh"
#include "gtest/gtest.h"

namespace strij::nodeagent {
namespace {

// Shared, test-owned outcome log: registry erasure (final result or error
// path) destroys the receiver object, so assertions read test-owned state.
struct ReceiverLog {
  bool delivered{false};
  bool is_final{false};
  std::string body;
  std::string error;
};

class RecordingReceiver final : public gateway::ResultReceiver {
public:
  std::shared_ptr<ReceiverLog> log = std::make_shared<ReceiverLog>();

  void Deliver(std::span<const std::byte> value, bool is_final) override {
    log->delivered = true;
    log->is_final = is_final;
    log->body.assign(reinterpret_cast<const char*>(value.data()), value.size());
  }

  void DeliverError(std::string_view reason) override { log->error = std::string(reason); }
};

class ChildSubmissionServiceTest : public ::testing::Test {
protected:
  static auto MakeCaps() -> std::shared_ptr<const node::NodeCapabilities> {
    auto caps = std::make_shared<node::NodeCapabilities>();
    caps->set_node_id("node-test");
    caps->set_capability_version(1);
    auto* pool = caps->add_pools();
    pool->set_name("cpu");
    pool->set_total(16);
    return caps;
  }

  static auto MakeEchoManager() -> std::shared_ptr<TaskHandlerManager> {
    auto manager = std::make_shared<TaskHandlerManager>();
    manager->AddHandler("echo", std::make_unique<nodeagent::task_handlers::EchoTaskHandler>());
    return manager;
  }

  void SetUp() override {
    dispatcher_ = std::make_shared<event::MockDispatcher>();
    admission_ = std::make_shared<AdmissionControllerImpl>(*MakeCaps(), *dispatcher_);
    run_task_service_ =
        std::make_unique<RunTaskServiceImpl>(MakeEchoManager(), admission_);
    registry_ = std::make_unique<LocalReceiverRegistry>();
    submission_ = std::make_unique<ChildSubmissionService>(*registry_, *run_task_service_,
                                                           admission_);
  }

  static auto MakeChild(const std::string& id, const std::string& type, int cpu) -> task::Task {
    task::Task child;
    child.set_id(id);
    child.set_type(type);
    child.set_body("payload");
    (*child.mutable_requirements()->mutable_resources())["cpu"] = cpu;
    return child;
  }

  std::shared_ptr<event::MockDispatcher> dispatcher_;
  std::shared_ptr<AdmissionController> admission_;
  std::unique_ptr<RunTaskServiceImpl> run_task_service_;
  std::unique_ptr<LocalReceiverRegistry> registry_;
  std::unique_ptr<ChildSubmissionService> submission_;
};

TEST_F(ChildSubmissionServiceTest, AdmittedChildRunsLocallyAndResolvesReceiver) {
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  submission_->Submit(MakeChild("child-1", "echo", 4), std::move(receiver));

  // The echo handler delivered the final result synchronously through the
  // RegistryResultSender bound to the registry entry.
  ASSERT_TRUE(log->delivered);
  EXPECT_TRUE(log->is_final);
  EXPECT_EQ(log->body, "payload");
  EXPECT_TRUE(log->error.empty());
  EXPECT_TRUE(registry_->Empty());
  EXPECT_EQ(admission_->InFlight("echo"), 0U);
  EXPECT_EQ(admission_->SharedFree("cpu"), 16U);
}

TEST_F(ChildSubmissionServiceTest, UnhandledChildDeliversErrorAndErasesEntry) {
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  submission_->Submit(MakeChild("child-1", "ghost", 4), std::move(receiver));

  ASSERT_FALSE(log->delivered);
  EXPECT_NE(log->error.find("no local capacity"), std::string::npos) << "error='" << log->error
                                                                     << "'";
  EXPECT_TRUE(registry_->Empty());
}

TEST_F(ChildSubmissionServiceTest, UnadmittedChildDeliversErrorAndErasesEntry) {
  // Consume all 16 cpu with an in-flight task so the child's 4-cpu request
  // fails admission: without a forward path the receiver gets an error and the
  // registry entry is erased (nothing hangs).
  ASSERT_TRUE(admission_->Admit("echo", MakeChild("a", "echo", 16).requirements()).ok());
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  submission_->Submit(MakeChild("child-1", "echo", 4), std::move(receiver));

  ASSERT_FALSE(log->delivered);
  EXPECT_NE(log->error.find("no local capacity"), std::string::npos) << "error='" << log->error
                                                                     << "'";
  EXPECT_TRUE(registry_->Empty());
}

TEST_F(ChildSubmissionServiceTest, PushSchedulerLocalRunReachesRegistryReceiver) {
  // End-to-end through the push scheduler's Schedule facet (child-policy step):
  // a child submitted to the local scheduler's Schedule resolves through the
  // receiver registered in the local registry.
  nodeagent::schedulers::PushLocalScheduler scheduler(*run_task_service_, *submission_);
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  scheduler.Schedule(MakeChild("child-1", "echo", 4), std::move(receiver));

  ASSERT_TRUE(log->delivered);
  EXPECT_TRUE(log->is_final);
  EXPECT_EQ(log->body, "payload");
  EXPECT_TRUE(registry_->Empty());
}

} // namespace
} // namespace strij::nodeagent