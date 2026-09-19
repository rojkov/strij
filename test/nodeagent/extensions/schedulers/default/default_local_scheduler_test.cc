#include <array>
#include <bit>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"
#include "test/mocks/extensions/extensions_mocks.hh"

#include "absl/status/status.h"
#include "common/core/io/connection.hh"
#include "common/core/io/protocol_parser.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/node/capabilities.pb.h"
#include "common/task/task.pb.h"
#include "nodeagent/core/admission_controller.hh"
#include "nodeagent/core/child_forwarder.hh"
#include "nodeagent/core/run_task_service.hh"
#include "nodeagent/core/task_handler_manager.hh"
#include "nodeagent/extensions/schedulers/default/default_local_scheduler.hh"
#include "nodeagent/extensions/task_handlers/echo/echo_task_handler.hh"
#include "strij/extensions/scheduler.hh"
#include "strij/gateway/result_receiver_storage.hh"

#include "gtest/gtest.h"

namespace strij::nodeagent {
namespace {

// Shared, test-owned outcome log: storage erasure (final result or error
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

// Recording ChildForwarder stub: records each Forward and returns a
// configurable status.
class StubForwarder final : public ChildForwarder {
public:
  absl::Status status = absl::OkStatus();
  int calls{0};
  std::string last_task_id;

  auto Forward(const task::Task& task) -> absl::Status override {
    ++calls;
    last_task_id = task.id();
    return status;
  }
};

class DefaultLocalSchedulerTest : public ::testing::Test {
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

  static auto MakeChild(const std::string& id, const std::string& type, int cpu) -> task::Task {
    task::Task child;
    child.set_id(id);
    child.set_type(type);
    child.set_body("payload");
    (*child.mutable_requirements()->mutable_resources())["cpu"] = cpu;
    return child;
  }

  // The returned TlvFrame spans `frame_storage_`, which lives for the duration
  // of the test (each test consumes at most one frame).
  auto MakeResultFrame(const std::string& id, const std::string& body, bool is_final)
      -> io::TlvFrame {
    task::TaskResult result;
    result.set_id(id);
    result.set_body(body);
    result.set_is_final(is_final);
    frame_storage_.clear();
    result.SerializeToString(&frame_storage_);
    return io::TlvFrame{io::TlvFrame::kResult,
                        std::as_bytes(std::span(frame_storage_.data(), frame_storage_.size()))};
  }

  auto MakeRejectedFrame(const std::string& id, const std::string& reason) -> io::TlvFrame {
    task::TaskRejected rejected;
    rejected.set_id(id);
    rejected.set_reason(reason);
    frame_storage_.clear();
    rejected.SerializeToString(&frame_storage_);
    return io::TlvFrame{io::TlvFrame::kTaskRejected,
                        std::as_bytes(std::span(frame_storage_.data(), frame_storage_.size()))};
  }

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
    admission_ = std::make_shared<AdmissionControllerImpl>(*MakeCaps(), *dispatcher_);
    run_task_service_ = std::make_unique<RunTaskServiceImpl>(MakeEchoManager(), admission_);
    scheduler_ = std::make_unique<nodeagent::schedulers::DefaultLocalScheduler>(*run_task_service_, admission_, nullptr);
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
  std::shared_ptr<AdmissionController> admission_;
  std::unique_ptr<RunTaskServiceImpl> run_task_service_;
  std::unique_ptr<nodeagent::schedulers::DefaultLocalScheduler> scheduler_;
  std::string frame_storage_;
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(DefaultLocalSchedulerTest, AdmittedChildRunsLocallyAndResolvesReceiver) {
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  scheduler_->Schedule(MakeChild("child-1", "echo", 4), std::move(receiver));

  // The echo handler delivered the final result synchronously through the
  // StorageResultSender bound to this scheduler's storage entry.
  ASSERT_TRUE(log->delivered);
  EXPECT_TRUE(log->is_final);
  EXPECT_EQ(log->body, "payload");
  EXPECT_TRUE(log->error.empty());
  EXPECT_TRUE(scheduler_->Storage().Empty());
  EXPECT_EQ(admission_->InFlight("echo"), 0U);
  EXPECT_EQ(admission_->SharedFree("cpu"), 16U);
}

TEST_F(DefaultLocalSchedulerTest, UnhandledChildDeliversErrorAndErasesEntry) {
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  scheduler_->Schedule(MakeChild("child-1", "ghost", 4), std::move(receiver));

  ASSERT_FALSE(log->delivered);
  EXPECT_NE(log->error.find("no local capacity"), std::string::npos) << "error='" << log->error
                                                                     << "'";
  EXPECT_TRUE(scheduler_->Storage().Empty());
}

TEST_F(DefaultLocalSchedulerTest, UnadmittedChildDeliversErrorAndErasesEntry) {
  // Consume all 16 cpu with an in-flight task so the child's 4-cpu request
  // fails admission: without a forward path the receiver gets an error and the
  // storage entry is erased (nothing hangs).
  ASSERT_TRUE(admission_->Admit("echo", MakeChild("a", "echo", 16).requirements()).ok());
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  scheduler_->Schedule(MakeChild("child-1", "echo", 4), std::move(receiver));

  ASSERT_FALSE(log->delivered);
  EXPECT_NE(log->error.find("no local capacity"), std::string::npos) << "error='" << log->error
                                                                     << "'";
  EXPECT_TRUE(scheduler_->Storage().Empty());
}

TEST_F(DefaultLocalSchedulerTest, ForwardedChildKeepsEntryUntilResultFrame) {
  StubForwarder forwarder;
  scheduler_ = std::make_unique<nodeagent::schedulers::DefaultLocalScheduler>(*run_task_service_, admission_, &forwarder);

  // Unhandled type with a live forward path: forwarded upstream, the receiver
  // stays registered under the child id awaiting the outcome frame.
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  scheduler_->Schedule(MakeChild("child-1", "ghost", 4), std::move(receiver));

  EXPECT_EQ(forwarder.calls, 1);
  EXPECT_EQ(forwarder.last_task_id, "child-1");
  EXPECT_FALSE(log->delivered);
  EXPECT_TRUE(log->error.empty());
  EXPECT_EQ(scheduler_->Storage().Size(), 1U);

  // The child's outcome returns as a kResult frame: the scheduler resolves and
  // erases the very entry created above.
  const absl::Status status =
      scheduler_->HandleFrame(MakeResultFrame("child-1", "remote", true), *conn_);
  EXPECT_TRUE(status.ok());
  ASSERT_TRUE(log->delivered);
  EXPECT_TRUE(log->is_final);
  EXPECT_EQ(log->body, "remote");
  EXPECT_TRUE(scheduler_->Storage().Empty());
}

TEST_F(DefaultLocalSchedulerTest, FailedForwardDeliversErrorAndErasesEntry) {
  StubForwarder forwarder;
  forwarder.status = absl::UnavailableError("no gateways");
  scheduler_ = std::make_unique<nodeagent::schedulers::DefaultLocalScheduler>(*run_task_service_, admission_, &forwarder);

  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  scheduler_->Schedule(MakeChild("child-1", "ghost", 4), std::move(receiver));

  EXPECT_EQ(forwarder.calls, 1);
  ASSERT_FALSE(log->delivered);
  EXPECT_NE(log->error.find("no local capacity"), std::string::npos) << "error='" << log->error
                                                                     << "'";
  EXPECT_TRUE(scheduler_->Storage().Empty());
}

TEST_F(DefaultLocalSchedulerTest, AdmittedChildRunsLocallyEvenWithForwardPath) {
  StubForwarder forwarder;
  scheduler_ = std::make_unique<nodeagent::schedulers::DefaultLocalScheduler>(*run_task_service_, admission_, &forwarder);

  // Local capacity wins over forwarding: an admitted child never touches the
  // forwarder.
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  scheduler_->Schedule(MakeChild("child-1", "echo", 4), std::move(receiver));

  EXPECT_EQ(forwarder.calls, 0);
  ASSERT_TRUE(log->delivered);
  EXPECT_TRUE(log->is_final);
  EXPECT_EQ(log->body, "payload");
  EXPECT_TRUE(scheduler_->Storage().Empty());
}

TEST_F(DefaultLocalSchedulerTest, ResultFrameDeliversAndErasesOnFinal) {
  // A receiver registered for a forwarded child (or any pre-registered entry):
  // resolved by the kResult frame path.
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  scheduler_->Storage().Put("child-1", std::move(receiver));

  const absl::Status status =
      scheduler_->HandleFrame(MakeResultFrame("child-1", "hello", true), *conn_);
  EXPECT_TRUE(status.ok());
  ASSERT_TRUE(log->delivered);
  EXPECT_TRUE(log->is_final);
  EXPECT_EQ(log->body, "hello");
  EXPECT_TRUE(log->error.empty());
  EXPECT_TRUE(scheduler_->Storage().Empty());
}

TEST_F(DefaultLocalSchedulerTest, NonFinalResultFrameKeepsEntry) {
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  scheduler_->Storage().Put("child-1", std::move(receiver));

  const absl::Status status =
      scheduler_->HandleFrame(MakeResultFrame("child-1", "chunk", false), *conn_);
  EXPECT_TRUE(status.ok());
  ASSERT_TRUE(log->delivered);
  EXPECT_FALSE(log->is_final);
  EXPECT_EQ(log->body, "chunk");
  EXPECT_EQ(scheduler_->Storage().Size(), 1U);
}

TEST_F(DefaultLocalSchedulerTest, ResultFrameForUnknownChildDrops) {
  const absl::Status status =
      scheduler_->HandleFrame(MakeResultFrame("ghost", "x", true), *conn_);
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(scheduler_->Storage().Empty());
}

TEST_F(DefaultLocalSchedulerTest, RejectedFrameDeliversErrorAndErases) {
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  scheduler_->Storage().Put("child-1", std::move(receiver));

  const absl::Status status =
      scheduler_->HandleFrame(MakeRejectedFrame("child-1", "queue full"), *conn_);
  EXPECT_TRUE(status.ok());
  ASSERT_FALSE(log->delivered);
  EXPECT_EQ(log->error, "queue full");
  EXPECT_TRUE(scheduler_->Storage().Empty());
}

TEST_F(DefaultLocalSchedulerTest, RejectedFrameForUnknownChildDrops) {
  const absl::Status status =
      scheduler_->HandleFrame(MakeRejectedFrame("ghost", "queue full"), *conn_);
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(scheduler_->Storage().Empty());
}

TEST_F(DefaultLocalSchedulerTest, HandledFrameTypesAreOnlyChildOutcomeFrames) {
  EXPECT_EQ(scheduler_->HandledFrameTypes().size(), 2U);
  EXPECT_TRUE(std::ranges::find(scheduler_->HandledFrameTypes(), io::TlvFrame::kResult) !=
              scheduler_->HandledFrameTypes().end());
  EXPECT_TRUE(std::ranges::find(scheduler_->HandledFrameTypes(), io::TlvFrame::kTaskRejected) !=
              scheduler_->HandledFrameTypes().end());
}

TEST_F(DefaultLocalSchedulerTest, AdvertisesNoSchedulingProtocol) {
  EXPECT_TRUE(scheduler_->RequiredProtocol().empty());
}

TEST_F(DefaultLocalSchedulerTest, FactoryCreateBuildsSchedulerFromContextServices) {
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
  StubForwarder forwarder;

  extensions::MockNodeagentFactoryContext context;
  EXPECT_CALL(context, RunTaskService()).WillRepeatedly(::testing::ReturnRef(run_task_service));
  EXPECT_CALL(context, AdmissionController()).WillRepeatedly(::testing::Return(admission));
  EXPECT_CALL(context, ChildForwarder()).WillRepeatedly(::testing::ReturnRef(forwarder));

  nodeagent::schedulers::DefaultLocalSchedulerFactory factory;
  EXPECT_EQ(factory.Name(), "default");
  EXPECT_TRUE(factory.RequiredProtocol().empty());
  auto config = factory.CreateEmptyConfigProto();
  ASSERT_NE(config, nullptr);
  auto scheduler = factory.Create(*config, context);
  ASSERT_NE(scheduler, nullptr);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent