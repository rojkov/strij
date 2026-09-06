#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"
#include "test/mocks/extensions/extensions_mocks.hh"

#include "common/core/io/connection.hh"
#include "common/core/io/protocol_parser.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/node/capabilities.pb.h"
#include "common/task/probe.pb.h"
#include "common/task/task.pb.h"
#include "nodeagent/core/admission_controller.hh"
#include "nodeagent/core/run_task_service.hh"
#include "nodeagent/core/task_handler_manager.hh"
#include "nodeagent/extensions/schedulers/probe/probe.pb.h"
#include "nodeagent/extensions/schedulers/probe/probe_local_scheduler.hh"
#include "nodeagent/extensions/task_handlers/echo/echo_task_handler.hh"
#include "strij/event/command.hh"
#include "gtest/gtest.h"

namespace strij::nodeagent::schedulers::probe {
namespace {

struct WireFrame {
  uint8_t type{0};
  std::vector<std::byte> payload;
};

class ProbeLocalSchedulerTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds_.data()));
    dispatcher_ = std::make_shared<event::MockDispatcher>();
    EXPECT_CALL(*dispatcher_,
                PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return());
    conn_ = std::make_unique<io::Connection>(fds_[0], dispatcher_, &owner_,
                                             [](io::Connection&) -> io::ProtocolParserPtr {
                                               return std::make_unique<io::TrivialParser>();
                                             });
    // Mirror the real event loop: any capacity release delivers CAPACITY_RELEASED
    // to the registered observer (which the scheduler registers at construction),
    // so the queue walk runs synchronously in tests.
    EXPECT_CALL(*dispatcher_, SubmitCommand(::testing::_))
        .WillRepeatedly(::testing::Invoke([](event::Command cmd) {
          if (cmd.destination_ != nullptr) {
            cmd.destination_->ProcessCommand(cmd);
          }
        }));
  }

  void TearDown() override {
    conn_.reset();
    close(fds_[0]);
    close(fds_[1]);
  }

  static auto MakePubCaps() -> std::shared_ptr<const node::NodeCapabilities> {
    auto caps = std::make_shared<node::NodeCapabilities>();
    caps->set_node_id("node-test");
    caps->set_capability_version(1);
    auto* pool = caps->add_pools();
    pool->set_name("cpu");
    pool->set_total(18);
    return caps;
  }

  static auto MakeEchoManager() -> std::shared_ptr<TaskHandlerManager> {
    auto manager = std::make_shared<TaskHandlerManager>();
    manager->AddHandler("echo", std::make_unique<nodeagent::task_handlers::EchoTaskHandler>());
    return manager;
  }

  // Writes any queued outputs to the socket and completes each write so the
  // connection drains its write queue (multiple frames need this).
  void ExpectWrites() {
    EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
        .WillRepeatedly(::testing::Invoke([this](event::Completable* /*io*/, uint8_t tag, int fd,
                                                 std::span<const std::byte> buf, off_t /*off*/) {
          ::write(fd, buf.data(), buf.size());
          conn_->HandleCompletion(tag, static_cast<int>(buf.size()), 0);
        }));
  }

  void ExpectNoWrites() {
    EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
        .Times(0);
  }

  static auto MakeProbeBytes(const std::string& id, int cpu) -> std::string {
    task::TaskProbe probe;
    probe.set_id(id);
    probe.set_type("echo");
    (*probe.mutable_requirements()->mutable_resources())["cpu"] = cpu;
    std::string serialized;
    probe.SerializeToString(&serialized);
    return serialized;
  }

  auto SendProbe(const std::string& id, int cpu) -> absl::Status {
    std::string serialized = MakeProbeBytes(id, cpu);
    return scheduler_->HandleFrame(
        {.type_id = io::TlvFrame::kTaskProbe,
         .value = std::as_bytes(std::span(serialized.data(), serialized.size()))},
        *conn_);
  }

  auto SendProbeRaw(const std::string& serialized) -> absl::Status {
    return scheduler_->HandleFrame(
        {.type_id = io::TlvFrame::kTaskProbe,
         .value = std::as_bytes(std::span(serialized.data(), serialized.size()))},
        *conn_);
  }

  auto SendCancel(const std::string& id) -> absl::Status {
    task::TaskProbeCancel cancel;
    cancel.set_id(id);
    std::string serialized;
    cancel.SerializeToString(&serialized);
    return scheduler_->HandleFrame(
        {.type_id = io::TlvFrame::kTaskProbeCancel,
         .value = std::as_bytes(std::span(serialized.data(), serialized.size()))},
        *conn_);
  }

  auto ReadFrames() -> std::vector<WireFrame> {
    std::array<std::byte, 4096> buf{};
    const ssize_t n = ::recv(fds_[1], buf.data(), buf.size(), MSG_DONTWAIT);
    if (n <= 0) {
      return {};
    }
    std::vector<WireFrame> frames;
    size_t off = 0;
    while (off + 5 <= static_cast<size_t>(n)) {
      const uint8_t type = static_cast<uint8_t>(buf[off]);
      uint32_t net_len{};
      std::memcpy(&net_len, buf.data() + off + 1, 4);
      const uint32_t len = ntohl(net_len);
      auto begin = std::next(buf.begin(), static_cast<std::ptrdiff_t>(off) + 5);
      frames.push_back(
          {.type = type, .payload = std::vector<std::byte>(begin, begin + len)});
      off += 5U + len;
    }
    return frames;
  }

  static auto ParsePull(const WireFrame& frame) -> task::TaskPull {
    task::TaskPull pull;
    pull.ParseFromArray(frame.payload.data(), static_cast<int>(frame.payload.size()));
    return pull;
  }

  static auto ParseDecline(const WireFrame& frame) -> task::TaskDecline {
    task::TaskDecline decline;
    decline.ParseFromArray(frame.payload.data(), static_cast<int>(frame.payload.size()));
    return decline;
  }

  static auto ParseTask(const WireFrame& frame) -> task::Task {
    task::Task task;
    task.ParseFromArray(frame.payload.data(), static_cast<int>(frame.payload.size()));
    return task;
  }

  static auto ParseResult(const WireFrame& frame) -> task::TaskResult {
    task::TaskResult result;
    result.ParseFromArray(frame.payload.data(), static_cast<int>(frame.payload.size()));
    return result;
  }

  std::array<int, 2> fds_{};
  std::shared_ptr<event::MockDispatcher> dispatcher_;
  event::DummyOwner owner_;
  io::ConnectionPtr conn_;
  std::shared_ptr<AdmissionController> admission_;
  std::unique_ptr<ProbeLocalScheduler> scheduler_;
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(ProbeLocalSchedulerTest, FreeCapacityPullsImmediately) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = std::make_unique<ProbeLocalScheduler>(run_task_service, admission_,
                                                     /*queue_capacity=*/4,
                                                     /*max_concurrent_preallocations=*/4);
  ExpectWrites();

  ASSERT_TRUE(SendProbe("1", 10).ok());

  auto frames = ReadFrames();
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[0]).id(), "1");
  EXPECT_EQ(admission_->SharedFree("cpu"), 8U);
  EXPECT_EQ(admission_->InFlight("echo"), 1U);
}

TEST_F(ProbeLocalSchedulerTest, ExhaustedCapacityEnqueuesProbeWithoutPulling) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = std::make_unique<ProbeLocalScheduler>(run_task_service, admission_,
                                                     /*queue_capacity=*/4,
                                                     /*max_concurrent_preallocations=*/4);
  ExpectWrites();

  ASSERT_TRUE(SendProbe("1", 10).ok());
  // Pool is 18, A holds 10, so 8 remain: B (10) does not fit → enqueued.
  ASSERT_TRUE(SendProbe("2", 10).ok());

  auto frames = ReadFrames();
  // Exactly one pull (A); no decline for the queued B.
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[0]).id(), "1");
  EXPECT_EQ(admission_->SharedFree("cpu"), 8U);
  EXPECT_EQ(admission_->InFlight("echo"), 1U);
}

TEST_F(ProbeLocalSchedulerTest, FullQueueDeclinesProbe) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = std::make_unique<ProbeLocalScheduler>(run_task_service, admission_,
                                                     /*queue_capacity=*/2,
                                                     /*max_concurrent_preallocations=*/4);
  ExpectWrites();

  ASSERT_TRUE(SendProbe("1", 10).ok()); // admitted, pulled
  ASSERT_TRUE(SendProbe("2", 10).ok()); // queued
  ASSERT_TRUE(SendProbe("3", 10).ok()); // queued (queue full)
  ASSERT_TRUE(SendProbe("4", 10).ok()); // no room → decline

  auto frames = ReadFrames();
  ASSERT_EQ(frames.size(), 2U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[0]).id(), "1");
  EXPECT_EQ(frames[1].type, io::TlvFrame::kTaskDecline);
  const task::TaskDecline decline = ParseDecline(frames[1]);
  EXPECT_EQ(decline.id(), "4");
  EXPECT_NE(decline.reason().find("queue full"), std::string::npos);
  EXPECT_EQ(admission_->SharedFree("cpu"), 8U);
}

TEST_F(ProbeLocalSchedulerTest, UnsatisfiableRequirementsDeclineImmediately) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = std::make_unique<ProbeLocalScheduler>(run_task_service, admission_,
                                                     /*queue_capacity=*/4,
                                                     /*max_concurrent_preallocations=*/4);
  ExpectWrites();

  // "gpu.h100" is undeclared: never satisfiable, so it must not be enqueued.
  task::TaskProbe probe;
  probe.set_id("1");
  probe.set_type("echo");
  (*probe.mutable_requirements()->mutable_resources())["gpu.h100"] = 1;
  std::string serialized;
  probe.SerializeToString(&serialized);

  EXPECT_TRUE(scheduler_
                  ->HandleFrame({.type_id = io::TlvFrame::kTaskProbe,
                                 .value = std::as_bytes(std::span(serialized))},
                                *conn_)
                  .ok());

  auto frames = ReadFrames();
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskDecline);
  EXPECT_EQ(ParseDecline(frames[0]).id(), "1");
  EXPECT_NE(ParseDecline(frames[0]).reason().find("unsatisfiable"), std::string::npos);
  EXPECT_EQ(admission_->SharedFree("cpu"), 18U);
  EXPECT_EQ(admission_->InFlight("echo"), 0U);
}

TEST_F(ProbeLocalSchedulerTest, CapacityReleaseWalksQueueFifo) {
  // max_preallocations 2: the walk must not drain past the overbooking dial.
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = std::make_unique<ProbeLocalScheduler>(run_task_service, admission_,
                                                     /*queue_capacity=*/4,
                                                     /*max_concurrent_preallocations=*/2);
  ExpectWrites();

  ASSERT_TRUE(SendProbe("a", 10).ok()); // admitted, pulled
  ASSERT_TRUE(SendProbe("b", 10).ok()); // queued
  ASSERT_TRUE(SendProbe("c", 10).ok()); // queued

  // Cancelling a releases its preallocation → CAPACITY_RELEASED → walk picks
  // the oldest queued (b) up to the cap; c stays queued.
  ASSERT_TRUE(SendCancel("a").ok());

  auto frames = ReadFrames();
  ASSERT_EQ(frames.size(), 2U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[0]).id(), "a");
  EXPECT_EQ(frames[1].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[1]).id(), "b");
  EXPECT_EQ(admission_->SharedFree("cpu"), 8U);

  // Releasing b's reservation frees the cap again → c is drained.
  ASSERT_TRUE(SendCancel("b").ok());

  frames = ReadFrames();
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[0]).id(), "c");
  EXPECT_EQ(admission_->SharedFree("cpu"), 8U);
}

TEST_F(ProbeLocalSchedulerTest, GrantsPreallocatedTaskWithoutDoubleAdmit) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = std::make_unique<ProbeLocalScheduler>(run_task_service, admission_,
                                                     /*queue_capacity=*/4,
                                                     /*max_concurrent_preallocations=*/4);
  ExpectWrites();

  ASSERT_TRUE(SendProbe("g1", 2).ok());
  EXPECT_EQ(admission_->SharedFree("cpu"), 16U);
  EXPECT_EQ(admission_->InFlight("echo"), 1U);

  task::Task task;
  task.set_id("g1");
  task.set_type("echo");
  (*task.mutable_requirements()->mutable_resources())["cpu"] = 2;
  std::string serialized;
  task.SerializeToString(&serialized);
  ASSERT_TRUE(scheduler_
                  ->HandleFrame({.type_id = io::TlvFrame::kTaskGrant,
                                 .value = std::as_bytes(std::span(serialized))},
                                *conn_)
                  .ok());

  auto frames = ReadFrames();
  ASSERT_EQ(frames.size(), 2U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(frames[1].type, io::TlvFrame::kResult);
  const task::TaskResult result = ParseResult(frames[1]);
  EXPECT_EQ(result.id(), "g1");
  EXPECT_TRUE(result.is_final());

  // The grant path never admits a second time: capacity was held by the probe
  // preallocation, transferred at grant, and released exactly once by the
  // final result — fully restored (a double admit would leave 16).
  EXPECT_EQ(admission_->SharedFree("cpu"), 18U);
  EXPECT_EQ(admission_->InFlight("echo"), 0U);
}

TEST_F(ProbeLocalSchedulerTest, StrayGrantIsDropped) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = std::make_unique<ProbeLocalScheduler>(run_task_service, admission_,
                                                     /*queue_capacity=*/4,
                                                     /*max_concurrent_preallocations=*/4);
  ExpectNoWrites();

  task::Task task;
  task.set_id("ghost");
  task.set_type("echo");
  std::string serialized;
  task.SerializeToString(&serialized);
  ASSERT_TRUE(scheduler_
                  ->HandleFrame({.type_id = io::TlvFrame::kTaskGrant,
                                 .value = std::as_bytes(std::span(serialized))},
                                *conn_)
                  .ok());

  EXPECT_TRUE(ReadFrames().empty());
  EXPECT_EQ(admission_->SharedFree("cpu"), 18U);
  EXPECT_EQ(admission_->InFlight("echo"), 0U);
}

TEST_F(ProbeLocalSchedulerTest, CancelReleasesPreallocation) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = std::make_unique<ProbeLocalScheduler>(run_task_service, admission_,
                                                     /*queue_capacity=*/4,
                                                     /*max_concurrent_preallocations=*/4);
  ExpectWrites();

  ASSERT_TRUE(SendProbe("1", 10).ok());
  ASSERT_TRUE(SendCancel("1").ok());

  EXPECT_EQ(admission_->SharedFree("cpu"), 18U);
  EXPECT_EQ(admission_->InFlight("echo"), 0U);

  auto frames = ReadFrames();
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
}

TEST_F(ProbeLocalSchedulerTest, CancelStopsQueuedProbeFromEverPulling) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = std::make_unique<ProbeLocalScheduler>(run_task_service, admission_,
                                                     /*queue_capacity=*/4,
                                                     /*max_concurrent_preallocations=*/4);
  ExpectWrites();

  ASSERT_TRUE(SendProbe("a", 10).ok());
  ASSERT_TRUE(SendProbe("b", 10).ok()); // queued
  ASSERT_TRUE(SendCancel("b").ok());    // removed from queue

  // Releasing a (via its own cancel) walks an empty queue: b must never pull.
  ASSERT_TRUE(SendCancel("a").ok());

  auto frames = ReadFrames();
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[0]).id(), "a");
  EXPECT_EQ(admission_->SharedFree("cpu"), 18U);
}

TEST_F(ProbeLocalSchedulerTest, SpuriousCapacityReleaseIsNoop) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = std::make_unique<ProbeLocalScheduler>(run_task_service, admission_,
                                                     /*queue_capacity=*/4,
                                                     /*max_concurrent_preallocations=*/4);
  ExpectNoWrites();

  scheduler_->ProcessCommand({.type_ = event::Command::CAPACITY_RELEASED});

  EXPECT_TRUE(ReadFrames().empty());
  EXPECT_EQ(admission_->SharedFree("cpu"), 18U);
}

TEST_F(ProbeLocalSchedulerTest, MalformedProbeFrameIsRejected) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = std::make_unique<ProbeLocalScheduler>(run_task_service, admission_,
                                                     /*queue_capacity=*/4,
                                                     /*max_concurrent_preallocations=*/4);
  ExpectNoWrites();

  auto garbage =
      std::vector<std::byte>{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
  const absl::Status status = scheduler_->HandleFrame(
      {.type_id = io::TlvFrame::kTaskProbe, .value = garbage}, *conn_);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(ProbeLocalSchedulerTest, UnknownFrameTypeIsNotFound) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = std::make_unique<ProbeLocalScheduler>(run_task_service, admission_,
                                                     /*queue_capacity=*/4,
                                                     /*max_concurrent_preallocations=*/4);
  ExpectNoWrites();

  const absl::Status status =
      scheduler_->HandleFrame({.type_id = io::TlvFrame::kHeartbeat, .value = {}}, *conn_);
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

class StubRunTaskService final : public nodeagent::RunTaskService {
public:
  void RunTask(const task::Task& /*task*/, io::Connection& /*conn*/) override {}
  void RunTask(const task::Task& /*task*/, io::Connection& /*conn*/,
               AdmissionScopePtr /*reserved*/) override {}
};

TEST_F(ProbeLocalSchedulerTest, FactoryCreateValidatesConfig) {
  auto caps = std::make_shared<node::NodeCapabilities>();
  caps->set_node_id("node-test");
  caps->set_capability_version(1);
  auto* pool = caps->add_pools();
  pool->set_name("cpu");
  pool->set_total(18);

  auto dispatcher = std::make_shared<event::MockDispatcher>();
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(0);
  auto admission = std::make_shared<AdmissionControllerImpl>(*caps, *dispatcher);
  StubRunTaskService run_task_service;

  extensions::MockNodeagentFactoryContext context;
  EXPECT_CALL(context, AdmissionController()).WillRepeatedly(::testing::Return(admission));
  EXPECT_CALL(context, RunTaskService()).WillRepeatedly(::testing::ReturnRef(run_task_service));

  ProbeLocalSchedulerFactory factory;
  {
    auto config = std::make_unique<extensions::schedulers::probe::ProbeSchedulerConfig>();
    config->set_queue_capacity(0);
    EXPECT_EQ(factory.Create(*config, context), nullptr);
  }
  {
    auto config = std::make_unique<extensions::schedulers::probe::ProbeSchedulerConfig>();
    config->set_max_concurrent_preallocations(0);
    EXPECT_EQ(factory.Create(*config, context), nullptr);
  }
  {
    // Absent fields fall back to defaults.
    auto config = std::make_unique<extensions::schedulers::probe::ProbeSchedulerConfig>();
    EXPECT_NE(factory.Create(*config, context), nullptr);
  }
  {
    auto config = std::make_unique<extensions::schedulers::probe::ProbeSchedulerConfig>();
    config->set_queue_capacity(7);
    config->set_max_concurrent_preallocations(3);
    EXPECT_NE(factory.Create(*config, context), nullptr);
  }
}

TEST_F(ProbeLocalSchedulerTest, FactoryRejectsWrongConfigType) {
  auto caps = std::make_shared<node::NodeCapabilities>();
  caps->set_node_id("node-test");
  caps->set_capability_version(1);
  auto* pool = caps->add_pools();
  pool->set_name("cpu");
  pool->set_total(18);

  auto dispatcher = std::make_shared<event::MockDispatcher>();
  auto admission = std::make_shared<AdmissionControllerImpl>(*caps, *dispatcher);
  StubRunTaskService run_task_service;

  extensions::MockNodeagentFactoryContext context;
  EXPECT_CALL(context, AdmissionController()).WillRepeatedly(::testing::Return(admission));
  EXPECT_CALL(context, RunTaskService()).WillRepeatedly(::testing::ReturnRef(run_task_service));

  task::Task unrelated;
  ProbeLocalSchedulerFactory factory;
  EXPECT_EQ(factory.Create(unrelated, context), nullptr);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::schedulers::probe