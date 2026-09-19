#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"
#include "test/mocks/extensions/extensions_mocks.hh"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/core/io/connection.hh"
#include "common/core/io/protocol_parser.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/node/capabilities.pb.h"
#include "common/task/task.pb.h"
#include "nodeagent/config/nodeagent.pb.h"
#include "nodeagent/core/admission_controller.hh"
#include "nodeagent/core/nodeagent_scheduler_router.hh"
#include "nodeagent/core/run_task_service.hh"
#include "nodeagent/core/task_handler_manager.hh"
#include "nodeagent/extensions/task_handlers/echo/echo_task_handler.hh"
#include "strij/gateway/result_receiver_storage.hh"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strij::nodeagent {
namespace {

// Shared, test-owned outcome log: a receiver handed to Schedule may be resolved
// synchronously (and erased from a storage entry, destroying the receiver object),
// so assertions read test-owned state.
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

// Stub scheduler that records the frames routed to it via HandleFrame and
// claims a caller-provided set of frame type_ids.
class FrameClaimingScheduler final : public extensions::Scheduler {
public:
  explicit FrameClaimingScheduler(std::vector<uint8_t> types) : types_{std::move(types)} {}

  void Schedule(const task::Task& /*task*/, gateway::ResultReceiverPtr receiver) override {
    receiver->DeliverError("stub");
  }

  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override { return "stub"; }

  auto HandleFrame(const io::TlvFrame& frame, io::Connection& /*conn*/) -> absl::Status override {
    handled_.push_back(frame.type_id);
    return absl::OkStatus();
  }

  [[nodiscard]] auto HandledFrameTypes() const -> std::span<const uint8_t> override {
    return types_;
  }

  std::vector<uint8_t> types_;
  std::vector<uint8_t> handled_;
};

// A real Connection over a socketpair so HandleFrame seams have a connection to
// pass (the stubs ignore it).
class NodeagentSchedulerRouterFrameTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds_.data()));
    dispatcher_ = std::make_shared<event::MockDispatcher>();
    EXPECT_CALL(*dispatcher_,
                PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_,
                            ::testing::_))
        .WillOnce(::testing::Return());
    conn_ = std::make_unique<io::Connection>(fds_[0], dispatcher_, &owner_,
                                             [](io::Connection&) -> io::ProtocolParserPtr {
                                               return std::make_unique<io::TrivialParser>();
                                             });
  }

  void TearDown() override {
    conn_.reset();
    close(fds_[0]);
    close(fds_[1]);
  }

  std::array<int, 2> fds_{};
  std::shared_ptr<event::MockDispatcher> dispatcher_;
  event::DummyOwner owner_;
  io::ConnectionPtr conn_;
};

TEST(NodeagentSchedulerRouterTest, RoutesByClaimedTaskType) {
  auto claiming = std::make_unique<RecordingScheduler>();
  auto fallback = std::make_unique<RecordingScheduler>();
  RecordingScheduler* claiming_raw = claiming.get();
  RecordingScheduler* fallback_raw = fallback.get();

  std::vector<NodeagentSchedulerRouter::ChildRoutedScheduler> routed;
  routed.push_back({.scheduler = std::move(claiming), .task_type = "echo", .local_default = true});
  routed.push_back({.scheduler = std::move(fallback), .task_type = "other", .local_default = false});
  NodeagentSchedulerRouter router(std::move(routed));

  task::Task child;
  child.set_id("child-1");
  child.set_type("echo");
  router.Submit(child, std::make_unique<RecordingReceiver>());

  EXPECT_EQ(claiming_raw->scheduled_types_, std::vector<std::string>({"echo"}));
  EXPECT_TRUE(fallback_raw->scheduled_types_.empty());
}

TEST(NodeagentSchedulerRouterTest, FallsBackToLocalDefault) {
  auto typed = std::make_unique<RecordingScheduler>();
  auto fallback = std::make_unique<RecordingScheduler>();
  RecordingScheduler* typed_raw = typed.get();
  RecordingScheduler* fallback_raw = fallback.get();

  std::vector<NodeagentSchedulerRouter::ChildRoutedScheduler> routed;
  routed.push_back({.scheduler = std::move(typed), .task_type = "typed-only", .local_default = false});
  routed.push_back({.scheduler = std::move(fallback), .task_type = "", .local_default = true});
  NodeagentSchedulerRouter router(std::move(routed));

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

TEST(NodeagentSchedulerRouterTest, NoClaimWithoutDefaultDeliversError) {
  auto typed = std::make_unique<RecordingScheduler>();
  RecordingScheduler* typed_raw = typed.get();

  std::vector<NodeagentSchedulerRouter::ChildRoutedScheduler> routed;
  routed.push_back({.scheduler = std::move(typed), .task_type = "typed-only", .local_default = false});
  NodeagentSchedulerRouter router(std::move(routed));

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

TEST(NodeagentSchedulerRouterTest, NoRoleEntryNeverReceivesSubmissions) {
  auto no_role = std::make_unique<RecordingScheduler>();
  auto fallback = std::make_unique<RecordingScheduler>();
  RecordingScheduler* no_role_raw = no_role.get();
  RecordingScheduler* fallback_raw = fallback.get();

  // First entry declares neither a task_type nor local_default: it is a pure
  // wire-protocol counterpart and must never receive child submissions. The
  // default authority handles them instead.
  std::vector<NodeagentSchedulerRouter::ChildRoutedScheduler> routed;
  routed.push_back({.scheduler = std::move(no_role), .task_type = "", .local_default = false});
  routed.push_back({.scheduler = std::move(fallback), .task_type = "", .local_default = true});
  NodeagentSchedulerRouter router(std::move(routed));

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

TEST_F(NodeagentSchedulerRouterFrameTest, HandledFrameTypesIsDeduplicatedUnion) {
  auto a = std::make_unique<FrameClaimingScheduler>(
      std::vector<uint8_t>({io::TlvFrame::kTaskSubmission}));
  auto b = std::make_unique<FrameClaimingScheduler>(
      std::vector<uint8_t>({io::TlvFrame::kTaskSubmission, io::TlvFrame::kTaskProbe}));

  std::vector<NodeagentSchedulerRouter::ChildRoutedScheduler> routed;
  routed.push_back({.scheduler = std::move(a), .task_type = "", .local_default = false});
  routed.push_back({.scheduler = std::move(b), .task_type = "", .local_default = false});
  NodeagentSchedulerRouter router(std::move(routed));

  // Overlapping claims are deduplicated in the router's owned union.
  EXPECT_EQ(router.HandledFrameTypes().size(), 2U);
  EXPECT_TRUE(std::ranges::find(router.HandledFrameTypes(), io::TlvFrame::kTaskSubmission) !=
              router.HandledFrameTypes().end());
  EXPECT_TRUE(std::ranges::find(router.HandledFrameTypes(), io::TlvFrame::kTaskProbe) !=
              router.HandledFrameTypes().end());
}

TEST_F(NodeagentSchedulerRouterFrameTest, RoutesFrameToOwningConstituent) {
  auto result_owner =
      std::make_unique<FrameClaimingScheduler>(std::vector<uint8_t>({io::TlvFrame::kResult}));
  auto submission_owner = std::make_unique<FrameClaimingScheduler>(
      std::vector<uint8_t>({io::TlvFrame::kTaskSubmission}));
  FrameClaimingScheduler* result_raw = result_owner.get();
  FrameClaimingScheduler* submission_raw = submission_owner.get();

  std::vector<NodeagentSchedulerRouter::ChildRoutedScheduler> routed;
  routed.push_back({.scheduler = std::move(result_owner), .task_type = "", .local_default = false});
  routed.push_back(
      {.scheduler = std::move(submission_owner), .task_type = "", .local_default = false});
  NodeagentSchedulerRouter router(std::move(routed));

  io::TlvFrame result_frame{io::TlvFrame::kResult, {}};
  io::TlvFrame submission_frame{io::TlvFrame::kTaskSubmission, {}};

  EXPECT_TRUE(router.HandleFrame(result_frame, *conn_).ok());
  EXPECT_TRUE(router.HandleFrame(submission_frame, *conn_).ok());

  EXPECT_EQ(result_raw->handled_, std::vector<uint8_t>({io::TlvFrame::kResult}));
  EXPECT_EQ(submission_raw->handled_, std::vector<uint8_t>({io::TlvFrame::kTaskSubmission}));
}

TEST_F(NodeagentSchedulerRouterFrameTest, UnclaimedFrameTypeReturnsNotFound) {
  auto probe_owner = std::make_unique<FrameClaimingScheduler>(
      std::vector<uint8_t>({io::TlvFrame::kTaskProbe}));

  std::vector<NodeagentSchedulerRouter::ChildRoutedScheduler> routed;
  routed.push_back({.scheduler = std::move(probe_owner), .task_type = "", .local_default = false});
  NodeagentSchedulerRouter router(std::move(routed));

  io::TlvFrame unowned_frame{io::TlvFrame::kResult, {}};
  const absl::Status status = router.HandleFrame(unowned_frame, *conn_);
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

TEST(BuildNodeagentSchedulerRouterTest, EmptySchedulerListFails) {
  config::NodeAgentConfig config;
  extensions::MockNodeagentFactoryContext context;
  auto result = BuildNodeagentSchedulerRouter(config, context);
  ASSERT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("empty"), std::string::npos);
}

TEST(BuildNodeagentSchedulerRouterTest, DuplicateTaskTypeFails) {
  config::NodeAgentConfig config;
  config.add_schedulers()->mutable_extension()->set_name("push");
  config.mutable_schedulers(0)->set_task_type("echo");
  config.add_schedulers()->mutable_extension()->set_name("push");
  config.mutable_schedulers(1)->set_task_type("echo");
  extensions::MockNodeagentFactoryContext context;
  auto result = BuildNodeagentSchedulerRouter(config, context);
  ASSERT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("more than once"), std::string::npos);
}

TEST(BuildNodeagentSchedulerRouterTest, MultipleLocalDefaultsFail) {
  config::NodeAgentConfig config;
  config.add_schedulers()->mutable_extension()->set_name("push");
  config.mutable_schedulers(0)->set_task_type("a");
  config.mutable_schedulers(0)->set_local_default(true);
  config.add_schedulers()->mutable_extension()->set_name("push");
  config.mutable_schedulers(1)->set_task_type("b");
  config.mutable_schedulers(1)->set_local_default(true);
  extensions::MockNodeagentFactoryContext context;
  auto result = BuildNodeagentSchedulerRouter(config, context);
  ASSERT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("more than one local_default"), std::string::npos);
}

TEST(BuildNodeagentSchedulerRouterTest, UnknownSchedulerNameFails) {
  config::NodeAgentConfig config;
  config.add_schedulers()->mutable_extension()->set_name("ghost");
  extensions::MockNodeagentFactoryContext context;
  auto result = BuildNodeagentSchedulerRouter(config, context);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
  EXPECT_NE(result.status().message().find("ghost"), std::string::npos);
}

TEST(BuildNodeagentSchedulerRouterTest, EmptyTaskTypeIsNotADefault) {
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

  extensions::MockNodeagentFactoryContext context;
  EXPECT_CALL(context, RunTaskService()).WillRepeatedly(::testing::ReturnRef(run_task_service));

  auto result = BuildNodeagentSchedulerRouter(config, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value()->RoutedSchedulerCount(), 2U);
  // Both push entries claim the same frame type; the router deduplicates.
  EXPECT_EQ(result.value()->HandledFrameTypes().size(), 1U);
}

TEST(BuildNodeagentSchedulerRouterTest, ValidConfigBuildsRouterWithOwnedFrameUnion) {
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

  extensions::MockNodeagentFactoryContext context;
  EXPECT_CALL(context, RunTaskService()).WillRepeatedly(::testing::ReturnRef(run_task_service));

  auto result = BuildNodeagentSchedulerRouter(config, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value()->RoutedSchedulerCount(), 2U);

  // The router's frame demux claims the push entries' kTaskSubmission (the 
  // inbound-frame facet the nodeagent handler hands every frame to).
  EXPECT_EQ(result.value()->HandledFrameTypes().size(), 1U);
  EXPECT_TRUE(std::ranges::find(result.value()->HandledFrameTypes(),
                                io::TlvFrame::kTaskSubmission) !=
              result.value()->HandledFrameTypes().end());
}

} // namespace
} // namespace strij::nodeagent