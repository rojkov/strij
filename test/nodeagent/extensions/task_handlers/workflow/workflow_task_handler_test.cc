#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "test/mocks/event/mocks.hh"
#include "test/mocks/extensions/extensions_mocks.hh"

#include "common/node/capabilities.pb.h"
#include "common/task/task.pb.h"
#include "nodeagent/core/admission_controller.hh"
#include "nodeagent/core/child_scheduler_router.hh"
#include "nodeagent/core/run_task_service.hh"
#include "nodeagent/core/task_handler_manager.hh"
#include "nodeagent/extensions/schedulers/default/default_local_scheduler.hh"
#include "nodeagent/extensions/task_handlers/echo/echo_task_handler.hh"
#include "nodeagent/extensions/task_handlers/workflow/workflow.pb.h"
#include "nodeagent/extensions/task_handlers/workflow/workflow_task_handler.hh"
#include "strij/extensions/extension_registry.hh"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strij::nodeagent::task_handlers {
namespace {

class WorkflowTaskHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto caps = std::make_shared<node::NodeCapabilities>();
    caps->set_node_id("node-test");
    caps->set_capability_version(1);
    auto* pool = caps->add_pools();
    pool->set_name("cpu");
    pool->set_total(32);

    dispatcher_ = std::make_shared<event::MockDispatcher>();
    admission_ = std::make_shared<AdmissionControllerImpl>(*caps, *dispatcher_);

    auto manager = std::make_shared<TaskHandlerManager>();
    manager->AddHandler("echo", std::make_unique<EchoTaskHandler>());
    run_task_service_ = std::make_unique<RunTaskServiceImpl>(manager, admission_);

    // The bundled "default" scheduler is the local authority: it registers each
    // child's receiver in its own registry, runs it locally when admitted, or
    // errors it. No forward path is installed here (mirroring a node without
    // gateway connections), so delivered-deficient children error locally.
    auto default_scheduler =
        std::make_unique<nodeagent::schedulers::DefaultLocalScheduler>(*run_task_service_,
                                                                       admission_, nullptr);
    default_scheduler_raw_ = default_scheduler.get();
    std::vector<ChildSchedulerRouter::ChildRoutedScheduler> routed;
    routed.push_back(
        {.scheduler = std::move(default_scheduler), .task_type = "echo", .local_default = true});
    router_ = std::make_unique<ChildSchedulerRouter>(std::move(routed));
    handler_ = std::make_unique<WorkflowTaskHandler>(*router_);
  }

  static auto SerializePlan(const std::vector<std::pair<std::string, std::string>>& children)
      -> std::string {
    extensions::task_handlers::workflow::WorkflowPlan plan;
    for (const auto& [type, body_text] : children) {
      auto* spec = plan.add_children();
      spec->set_type(type);
      spec->set_body(body_text);
    }
    std::string serialized;
    plan.SerializeToString(&serialized);
    return serialized;
  }

  static auto MakeParent(const std::string& id, const std::string& body) -> task::Task {
    task::Task parent;
    parent.set_id(id);
    parent.set_type("workflow");
    parent.set_body(body);
    return parent;
  }

  std::shared_ptr<event::MockDispatcher> dispatcher_;
  std::shared_ptr<AdmissionController> admission_;
  std::unique_ptr<RunTaskServiceImpl> run_task_service_;
  nodeagent::schedulers::DefaultLocalScheduler* default_scheduler_raw_{nullptr};
  std::unique_ptr<ChildSchedulerRouter> router_;
  std::unique_ptr<WorkflowTaskHandler> handler_;
};

TEST_F(WorkflowTaskHandlerTest, FansOutAndAggregatesChildBodies) {
  task::Task parent = MakeParent("parent-1", SerializePlan({{"echo", "a"}, {"echo", "b"}}));

  auto sender = std::make_unique<extensions::MockResultSender>();
  task::TaskResult sent;
  EXPECT_CALL(*sender, Send(::testing::_)).WillOnce(::testing::SaveArg<0>(&sent));
  handler_->HandleTask(parent, std::move(sender));

  EXPECT_EQ(sent.id(), "parent-1");
  EXPECT_EQ(sent.body(), "ab");
  EXPECT_TRUE(sent.is_final());
  EXPECT_TRUE(default_scheduler_raw_->Registry().Empty());
  EXPECT_EQ(admission_->InFlight("echo"), 0U);
}

TEST_F(WorkflowTaskHandlerTest, AbortsOnChildError) {
  // The second child type has no handler: its submission resolves with an error
  // (no forward path in 4A), aggregation aborts, and the parent result describes
  // the failure instead of concatenating bodies.
  task::Task parent =
      MakeParent("parent-1", SerializePlan({{"echo", "a"}, {"ghost", "will-fail"}}));

  auto sender = std::make_unique<extensions::MockResultSender>();
  task::TaskResult sent;
  EXPECT_CALL(*sender, Send(::testing::_)).WillOnce(::testing::SaveArg<0>(&sent));
  handler_->HandleTask(parent, std::move(sender));

  EXPECT_EQ(sent.id(), "parent-1");
  EXPECT_NE(sent.body().find("failed"), std::string::npos);
  EXPECT_NE(sent.body().find("no local capacity"), std::string::npos);
  EXPECT_TRUE(sent.is_final());
  EXPECT_TRUE(default_scheduler_raw_->Registry().Empty());
}

TEST_F(WorkflowTaskHandlerTest, MalformedPlanDeliversErrorText) {
  task::Task parent = MakeParent("parent-1", "not-a-plan");

  auto sender = std::make_unique<extensions::MockResultSender>();
  task::TaskResult sent;
  EXPECT_CALL(*sender, Send(::testing::_)).WillOnce(::testing::SaveArg<0>(&sent));
  handler_->HandleTask(parent, std::move(sender));

  EXPECT_EQ(sent.id(), "parent-1");
  EXPECT_EQ(sent.body(), "malformed workflow plan");
  EXPECT_TRUE(sent.is_final());
}

TEST_F(WorkflowTaskHandlerTest, FactoryRetrievesChildTaskSubmitter) {
  auto& registry = extensions::Registry<nodeagent::TaskHandlerFactory>::instance();
  auto* factory = registry.GetFactory("workflow");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->Name(), "workflow");

  extensions::MockNodeagentFactoryContext context;
  EXPECT_CALL(context, ChildTaskSubmitter()).WillRepeatedly(::testing::ReturnRef(*router_));

  auto config = factory->CreateEmptyConfigProto();
  auto handler = factory->Create(*config, context);
  ASSERT_NE(handler, nullptr);
}

} // namespace
} // namespace strij::nodeagent::task_handlers