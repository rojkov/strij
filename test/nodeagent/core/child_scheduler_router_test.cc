#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/node/capabilities.pb.h"
#include "common/task/task.pb.h"
#include "nodeagent/config/nodeagent.pb.h"
#include "nodeagent/core/admission_controller.hh"
#include "nodeagent/core/child_scheduler_router.hh"
#include "nodeagent/core/child_submission_service.hh"
#include "nodeagent/core/local_receiver_registry.hh"
#include "nodeagent/core/run_task_service.hh"
#include "nodeagent/core/task_handler_manager.hh"
#include "nodeagent/extensions/schedulers/push/push_local_scheduler.hh"
#include "nodeagent/extensions/task_handlers/echo/echo_task_handler.hh"
#include "strij/gateway/result_receiver_storage.hh"
#include "test/mocks/event/mocks.hh"
#include "test/mocks/extensions/extensions_mocks.hh"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strij::nodeagent {
namespace {

// Shared, test-owned outcome log: a receiver handed to Schedule may be resolved
// synchronously (and, through ChildSubmissionService, erased from a registry,
// destroying the receiver object), so assertions read test-owned state.
struct ReceiverLog {
  bool delivered{false};
  std::string body;
  std::string error;
};

class RecordingReceiver final : public gateway::ResultReceiver {
public:
  std::shared_ptr<ReceiverLog> log = std::make_shared<ReceiverLog>();

  void Deliver(std::span<const std::byte> value, bool /*is_final*/) override {
    log->delivered = true;
    log->body.assign(reinterpret_cast<const char*>(value.data()), value.size());
  }

  void DeliverError(std::string_view reason) override { log->error = std::string(reason); }
};

// Stub scheduler that records each Schedule's task type and resolves the
// receiver with a fixed final result.
class RecordingScheduler final : public extensions::Scheduler {
public:
  void Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) override {
    scheduled_types_.push_back(task.type());
    const auto data = std::as_bytes(std::span("handled", 7));
    receiver->Deliver(data, /*is_final=*/true);
  }

  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override { return "stub"; }

  std::vector<std::string> scheduled_types_;
};

TEST(ChildSchedulerRouterTest, RoutesByClaimedTaskType) {
  auto claiming = std::make_unique<RecordingScheduler>();
  auto fallback = std::make_unique<RecordingScheduler>();
  RecordingScheduler* claiming_raw = claiming.get();
  RecordingScheduler* fallback_raw = fallback.get();

  std::vector<ChildSchedulerRouter::ChildRoutedScheduler> routed;
  routed.push_back({.scheduler = std::move(claiming), .task_type = "echo", .local_default = true});
  routed.push_back({.scheduler = std::move(fallback), .task_type = "other", .local_default = false});
  ChildSchedulerRouter router(std::move(routed));

  task::Task child;
  child.set_id("child-1");
  child.set_type("echo");
  router.Submit(child, std::make_unique<RecordingReceiver>());

  EXPECT_EQ(claiming_raw->scheduled_types_, std::vector<std::string>({"echo"}));
  EXPECT_TRUE(fallback_raw->scheduled_types_.empty());
}

TEST(ChildSchedulerRouterTest, FallsBackToLocalDefault) {
  auto typed = std::make_unique<RecordingScheduler>();
  auto fallback = std::make_unique<RecordingScheduler>();
  RecordingScheduler* typed_raw = typed.get();
  RecordingScheduler* fallback_raw = fallback.get();

  std::vector<ChildSchedulerRouter::ChildRoutedScheduler> routed;
  routed.push_back({.scheduler = std::move(typed), .task_type = "typed-only", .local_default = false});
  routed.push_back({.scheduler = std::move(fallback), .task_type = "", .local_default = true});
  ChildSchedulerRouter router(std::move(routed));

  task::Task child;
  child.set_id("child-1");
  child.set_type("unclaimed");
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  router.Submit(child, std::move(receiver));

  // The default entry runs the unclaimed type; the typed-only entry never does.
  EXPECT_TRUE(typed_raw->scheduled_types_.empty());
  EXPECT_EQ(fallback_raw->scheduled_types_, std::vector<std::string>({"unclaimed"}));
  EXPECT_TRUE(log->delivered);
}

TEST(ChildSchedulerRouterTest, NoClaimWithoutDefaultDeliversError) {
  auto typed = std::make_unique<RecordingScheduler>();
  RecordingScheduler* typed_raw = typed.get();

  std::vector<ChildSchedulerRouter::ChildRoutedScheduler> routed;
  routed.push_back({.scheduler = std::move(typed), .task_type = "typed-only", .local_default = false});
  ChildSchedulerRouter router(std::move(routed));

  task::Task child;
  child.set_id("child-1");
  child.set_type("unclaimed");
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  router.Submit(child, std::move(receiver));

  EXPECT_TRUE(typed_raw->scheduled_types_.empty());
  ASSERT_FALSE(log->delivered);
  EXPECT_NE(log->error.find("no local scheduler claims task type 'unclaimed'"), std::string::npos)
      << "error='" << log->error << "'";
}

TEST(ChildSchedulerRouterTest, NoRoleEntryNeverReceivesSubmissions) {
  auto no_role = std::make_unique<RecordingScheduler>();
  auto fallback = std::make_unique<RecordingScheduler>();
  RecordingScheduler* no_role_raw = no_role.get();
  RecordingScheduler* fallback_raw = fallback.get();

  // First entry declares neither a task_type nor local_default: it is a pure
  // wire-protocol counterpart and must never receive child submissions. The
  // default authority handles them instead.
  std::vector<ChildSchedulerRouter::ChildRoutedScheduler> routed;
  routed.push_back({.scheduler = std::move(no_role), .task_type = "", .local_default = false});
  routed.push_back({.scheduler = std::move(fallback), .task_type = "", .local_default = true});
  ChildSchedulerRouter router(std::move(routed));

  task::Task child;
  child.set_id("child-1");
  child.set_type("anything");
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  router.Submit(child, std::move(receiver));

  EXPECT_TRUE(no_role_raw->scheduled_types_.empty());
  EXPECT_EQ(fallback_raw->scheduled_types_, std::vector<std::string>({"anything"}));
  EXPECT_TRUE(log->delivered);
}

TEST(BuildChildSchedulerRouterTest, EmptySchedulerListFails) {
  config::NodeAgentConfig config;
  extensions::MockNodeagentFactoryContext context;
  auto result = BuildChildSchedulerRouter(config, context);
  ASSERT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("empty"), std::string::npos);
}

TEST(BuildChildSchedulerRouterTest, DuplicateTaskTypeFails) {
  config::NodeAgentConfig config;
  config.add_schedulers()->mutable_extension()->set_name("push");
  config.mutable_schedulers(0)->set_task_type("echo");
  config.add_schedulers()->mutable_extension()->set_name("push");
  config.mutable_schedulers(1)->set_task_type("echo");
  extensions::MockNodeagentFactoryContext context;
  auto result = BuildChildSchedulerRouter(config, context);
  ASSERT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("more than once"), std::string::npos);
}

TEST(BuildChildSchedulerRouterTest, MultipleLocalDefaultsFail) {
  config::NodeAgentConfig config;
  config.add_schedulers()->mutable_extension()->set_name("push");
  config.mutable_schedulers(0)->set_task_type("a");
  config.mutable_schedulers(0)->set_local_default(true);
  config.add_schedulers()->mutable_extension()->set_name("push");
  config.mutable_schedulers(1)->set_task_type("b");
  config.mutable_schedulers(1)->set_local_default(true);
  extensions::MockNodeagentFactoryContext context;
  auto result = BuildChildSchedulerRouter(config, context);
  ASSERT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("more than one local_default"), std::string::npos);
}

TEST(BuildChildSchedulerRouterTest, UnknownSchedulerNameFails) {
  config::NodeAgentConfig config;
  config.add_schedulers()->mutable_extension()->set_name("ghost");
  extensions::MockNodeagentFactoryContext context;
  auto result = BuildChildSchedulerRouter(config, context);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
  EXPECT_NE(result.status().message().find("ghost"), std::string::npos);
}

TEST(BuildChildSchedulerRouterTest, EmptyTaskTypeIsNotADefault) {
  // Two no-role entries (empty task_type, no local_default) are valid: the
  // config declares no fallback authority, an accepted shape for wire-only
  // counterpart schedulers.
  config::NodeAgentConfig config;
  config.add_schedulers()->mutable_extension()->set_name("push");
  config.add_schedulers()->mutable_extension()->set_name("push");

  auto caps = std::make_shared<node::NodeCapabilities>();
  caps->set_node_id("node-test");
  caps->set_capability_version(1);
  auto* pool = caps->add_pools();
  pool->set_name("cpu");
  pool->set_total(16);

  auto dispatcher = std::make_shared<event::MockDispatcher>();
  auto admission = std::make_shared<AdmissionControllerImpl>(*caps, *dispatcher);
  auto manager = std::make_shared<TaskHandlerManager>();
  manager->AddHandler("echo", std::make_unique<nodeagent::task_handlers::EchoTaskHandler>());
  RunTaskServiceImpl run_task_service(manager, admission);
  LocalReceiverRegistry registry;
  ChildSubmissionService submission(registry, run_task_service, admission);

  extensions::MockNodeagentFactoryContext context;
  EXPECT_CALL(context, RunTaskService()).WillRepeatedly(::testing::ReturnRef(run_task_service));
  EXPECT_CALL(context, ChildSubmissionService()).WillRepeatedly(::testing::ReturnRef(submission));

  auto result = BuildChildSchedulerRouter(config, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value()->RoutedSchedulerCount(), 2U);
  EXPECT_EQ(result.value()->LocalSchedulerPointers().size(), 2U);
}

TEST(BuildChildSchedulerRouterTest, ValidConfigBuildsRouterWithSchedulerPointers) {
  config::NodeAgentConfig config;
  config.add_schedulers()->mutable_extension()->set_name("push");
  config.mutable_schedulers(0)->set_task_type("echo");
  config.mutable_schedulers(0)->set_local_default(true);
  config.add_schedulers()->mutable_extension()->set_name("push");

  auto caps = std::make_shared<node::NodeCapabilities>();
  caps->set_node_id("node-test");
  caps->set_capability_version(1);
  auto* pool = caps->add_pools();
  pool->set_name("cpu");
  pool->set_total(16);

  auto dispatcher = std::make_shared<event::MockDispatcher>();
  auto admission = std::make_shared<AdmissionControllerImpl>(*caps, *dispatcher);
  auto manager = std::make_shared<TaskHandlerManager>();
  manager->AddHandler("echo", std::make_unique<nodeagent::task_handlers::EchoTaskHandler>());
  RunTaskServiceImpl run_task_service(manager, admission);
  LocalReceiverRegistry registry;
  ChildSubmissionService submission(registry, run_task_service, admission);

  extensions::MockNodeagentFactoryContext context;
  EXPECT_CALL(context, RunTaskService()).WillRepeatedly(::testing::ReturnRef(run_task_service));
  EXPECT_CALL(context, ChildSubmissionService()).WillRepeatedly(::testing::ReturnRef(submission));

  auto result = BuildChildSchedulerRouter(config, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value()->RoutedSchedulerCount(), 2U);

  // The router's constituent scheduler pointers are unique and non-null; a
  // gateway-facing frame dispatcher consumes this exact span.
  auto pointers = result.value()->LocalSchedulerPointers();
  ASSERT_EQ(pointers.size(), 2U);
  EXPECT_NE(pointers[0], nullptr);
  EXPECT_NE(pointers[1], nullptr);
  EXPECT_NE(pointers[0], pointers[1]);
}

} // namespace
} // namespace strij::nodeagent