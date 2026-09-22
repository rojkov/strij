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
#include "test/mocks/extensions/nodeagent_deps.hh"

#include "common/core/io/connection.hh"
#include "common/core/io/protocol_parser.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/node/capabilities.pb.h"
#include "common/task/probe.pb.h"
#include "common/task/task.pb.h"
#include "gtest/gtest.h"
#include "nodeagent/core/admission_controller.hh"
#include "nodeagent/core/data_dependency_fetcher_router.hh"
#include "nodeagent/core/object_cache.hh"
#include "nodeagent/core/run_task_service.hh"
#include "nodeagent/core/task_handler_manager.hh"
#include "nodeagent/extensions/schedulers/probe/probe.pb.h"
#include "nodeagent/extensions/schedulers/probe/probe_local_scheduler.hh"
#include "nodeagent/extensions/task_handlers/echo/echo_task_handler.hh"
#include "strij/event/command.hh"

namespace strij::nodeagent::schedulers::probe {
namespace {

struct WireFrame {
  uint8_t type{0};
  std::vector<std::byte> payload;
};

// Collects a child task's outcome resolved through the shared child-policy
// step: a delivered result or a delivered error. Storage erasure destroys the
// receiver, so assertions read the test-owned log.
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

// Records (ref, task_id) pairs instead of fetching; never submits
class RecordingFetcher final : public extensions::DataDependencyFetcher {
public:
  explicit RecordingFetcher(std::string source)
      : source_value_(std::move(source)), source_(source_value_) {}

  void Fetch(const task::DataRef& ref, const std::string& task_id,
             event::Dispatcher& /*dispatcher*/, event::CommandHandler* /*destination*/) override {
    fetched_.push_back({.source = ref.source(), .key = ref.key(), .task_id = task_id});
  }

  [[nodiscard]] auto HandledSourceTypes() const -> std::span<const std::string_view> override {
    return std::span<const std::string_view>(&source_, 1);
  }

  struct FetchCall {
    std::string source;
    std::string key;
    std::string task_id;
  };

  std::vector<FetchCall> fetched_;

private:
  std::string source_value_;
  std::string_view source_;
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
    EXPECT_CALL(*dispatcher_,
                PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
        .WillRepeatedly(::testing::Invoke([this](event::Completable* /*io*/, uint8_t tag, int fdesc,
                                                 std::span<const std::byte> buf, off_t /*off*/) {
          ::write(fdesc, buf.data(), buf.size());
          conn_->HandleCompletion(tag, static_cast<int>(buf.size()), 0);
        }));
  }

  void ExpectNoWrites() {
    EXPECT_CALL(*dispatcher_,
                PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
        .Times(0);
  }

  static auto MakeDepRef(const std::string& source, const std::string& key) -> task::DataRef {
    task::DataRef ref;
    ref.set_source(source);
    ref.set_key(key);
    return ref;
  }

  // Builds a real router over the fixture's object cache; fetchers may be
  // empty (no-ops: AllCached trivially true, FetchAll a no-op).
  void MakeRouter(std::vector<extensions::DataDependencyFetcherPtr> fetchers = {}) {
    auto result = DataDependencyFetcherRouter::Build(object_cache_, std::move(fetchers));
    ASSERT_TRUE(result.ok());
    router_ = std::move(result).value();
  }

  auto MakeScheduler(nodeagent::RunTaskService& run_task_service, size_t queue_capacity,
                     size_t max_concurrent_preallocations,
                     std::vector<extensions::DataDependencyFetcherPtr> fetchers = {})
      -> std::unique_ptr<ProbeLocalScheduler> {
    MakeRouter(std::move(fetchers));
    return std::make_unique<ProbeLocalScheduler>(run_task_service, admission_, *router_,
                                                 *dispatcher_, queue_capacity,
                                                 max_concurrent_preallocations);
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

  static auto MakeProbeBytes(const std::string& id, int cpu,
                             const google::protobuf::RepeatedPtrField<task::DataRef>& deps)
      -> std::string {
    task::TaskProbe probe;
    probe.set_id(id);
    probe.set_type("echo");
    (*probe.mutable_requirements()->mutable_resources())["cpu"] = cpu;
    probe.mutable_deps()->CopyFrom(deps);
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

  auto SendProbeWithDeps(const std::string& id, int cpu,
                         const google::protobuf::RepeatedPtrField<task::DataRef>& deps)
      -> absl::Status {
    return SendProbeRaw(MakeProbeBytes(id, cpu, deps));
  }

  // Synthesizes the dispatcher-side delivery of a finished dependency: walk()
  // runs synchronously through the MockDispatcher's SubmitCommand fallback in
  // SetUp. `task_id` must be process-stable (an lvalue kept alive by the test).
  void DeliverDepCompleted(const std::string& task_id) {
    event::Command cmd;
    cmd.type_ = event::Command::DEP_COMPLETED;
    cmd.args_ = const_cast<std::string*>(&task_id);
    scheduler_->ProcessCommand(cmd);
  }

  auto SendCancel(const std::string& task_id) -> absl::Status {
    task::TaskProbeCancel cancel;
    cancel.set_id(task_id);
    std::string serialized;
    cancel.SerializeToString(&serialized);
    return scheduler_->HandleFrame(
        {.type_id = io::TlvFrame::kTaskProbeCancel,
         .value = std::as_bytes(std::span(serialized.data(), serialized.size()))},
        *conn_);
  }

  auto ReadFrames() -> std::vector<WireFrame> {
    std::array<std::byte, 4096> buf{};
    const ssize_t num = ::recv(fds_[1], buf.data(), buf.size(), MSG_DONTWAIT);
    if (num <= 0) {
      return {};
    }
    std::vector<WireFrame> frames;
    size_t off = 0;
    while (off + 5 <= static_cast<size_t>(num)) {
      const uint8_t type = static_cast<uint8_t>(buf[off]);
      uint32_t net_len{};
      std::memcpy(&net_len, buf.data() + off + 1, 4);
      const uint32_t len = ntohl(net_len);
      auto* begin = std::next(buf.begin(), static_cast<std::ptrdiff_t>(off) + 5);
      frames.push_back({.type = type, .payload = std::vector<std::byte>(begin, begin + len)});
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
  InMemoryObjectCache object_cache_;
  std::shared_ptr<DataDependencyFetcherRouter> router_;
  std::unique_ptr<ProbeLocalScheduler> scheduler_;
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(ProbeLocalSchedulerTest, FreeCapacityPullsImmediately) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = MakeScheduler(run_task_service, 4, 4);
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
  scheduler_ = MakeScheduler(run_task_service, 4, 4);
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
  scheduler_ = MakeScheduler(run_task_service, 2, 4);
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
  scheduler_ = MakeScheduler(run_task_service, 4, 4);
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
  scheduler_ = MakeScheduler(run_task_service, 4, 2);
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
  scheduler_ = MakeScheduler(run_task_service, 4, 4);
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
  scheduler_ = MakeScheduler(run_task_service, 4, 4);
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
  scheduler_ = MakeScheduler(run_task_service, 4, 4);
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
  scheduler_ = MakeScheduler(run_task_service, 4, 4);
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
  scheduler_ = MakeScheduler(run_task_service, 4, 4);
  ExpectNoWrites();

  scheduler_->ProcessCommand({.type_ = event::Command::CAPACITY_RELEASED});

  EXPECT_TRUE(ReadFrames().empty());
  EXPECT_EQ(admission_->SharedFree("cpu"), 18U);
}

TEST_F(ProbeLocalSchedulerTest, MalformedProbeFrameIsRejected) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = MakeScheduler(run_task_service, 4, 4);
  ExpectNoWrites();

  auto garbage =
      std::vector<std::byte>{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
  const absl::Status status =
      scheduler_->HandleFrame({.type_id = io::TlvFrame::kTaskProbe, .value = garbage}, *conn_);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(ProbeLocalSchedulerTest, UnknownFrameTypeIsNotFound) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = MakeScheduler(run_task_service, 4, 4);
  ExpectNoWrites();

  const absl::Status status =
      scheduler_->HandleFrame({.type_id = io::TlvFrame::kHeartbeat, .value = {}}, *conn_);
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

TEST_F(ProbeLocalSchedulerTest, ProbeWithDepsEnqueuedPullsAfterDepCompletes) {
  auto ray = std::make_unique<RecordingFetcher>("ray");
  RecordingFetcher* fetcher = ray.get();
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  std::vector<extensions::DataDependencyFetcherPtr> fetchers;
  fetchers.push_back(std::move(ray));
  scheduler_ = MakeScheduler(run_task_service, 4, 4, std::move(fetchers));
  ExpectWrites();

  ASSERT_TRUE(SendProbe("1", 10).ok()); // admitted + pulled
  auto deps = google::protobuf::RepeatedPtrField<task::DataRef>{};
  *deps.Add() = MakeDepRef("ray", "objB");
  ASSERT_TRUE(SendProbeWithDeps("2", 10, deps).ok()); // enqueued (8 free < 10)

  // Deps prefetch started on enqueue, but no pull yet (capacity exhausted).
  ASSERT_EQ(fetcher->fetched_.size(), 1U);
  EXPECT_EQ(fetcher->fetched_[0].source, "ray");
  EXPECT_EQ(fetcher->fetched_[0].key, "objB");
  EXPECT_EQ(fetcher->fetched_[0].task_id, "2");

  auto frames = ReadFrames();
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[0]).id(), "1");

  // Freed capacity still cannot serve "2": the dep prefetch has not landed yet,
  // so the walk leaves it queued (capacity is not burned for a task that cannot
  // run). No CAPACITY_RELEASED frame exists — only commands travel in-band.
  std::string task_two = "2";
  ASSERT_TRUE(SendCancel("1").ok());
  EXPECT_TRUE(ReadFrames().empty());

  // Now the data arrives: DEP_COMPLETED triggers a walk that pulls "2".
  object_cache_.Populate(MakeDepRef("ray", "objB"), "payload-B");
  DeliverDepCompleted(task_two);

  frames = ReadFrames();
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[0]).id(), "2");
  EXPECT_EQ(admission_->InFlight("echo"), 1U);
}

TEST_F(ProbeLocalSchedulerTest, DepsMissingKeepsProbeQueuedOnCapacityRelease) {
  // "ray" is a registered but slow fetcher: the scheme gates readiness, and the
  // fetch is still in flight when capacity frees.
  auto ray = std::make_unique<RecordingFetcher>("ray");
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  std::vector<extensions::DataDependencyFetcherPtr> fetchers;
  fetchers.push_back(std::move(ray));
  scheduler_ = MakeScheduler(run_task_service, 4, 4, std::move(fetchers));
  ExpectWrites();

  ASSERT_TRUE(SendProbe("1", 10).ok()); // admitted + pulled
  auto deps = google::protobuf::RepeatedPtrField<task::DataRef>{};
  *deps.Add() = MakeDepRef("ray", "objB");
  ASSERT_TRUE(SendProbeWithDeps("2", 10, deps).ok()); // enqueued
  ASSERT_TRUE(SendCancel("1").ok());                  // CAPACITY_RELEASED

  // "2" fits the freed capacity but its dep is not cached: it must stay queued
  // (burning capacity would strand a task that cannot run).
  auto frames = ReadFrames();
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[0]).id(), "1");
  EXPECT_EQ(admission_->InFlight("echo"), 0U);
}

TEST_F(ProbeLocalSchedulerTest, EmptyDepsWalkImmediatelyOnCapacityRelease) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = MakeScheduler(run_task_service, 4, 4);
  ExpectWrites();

  ASSERT_TRUE(SendProbe("1", 10).ok()); // admitted + pulled
  ASSERT_TRUE(SendProbe("2", 10).ok()); // enqueued (empty deps)
  ASSERT_TRUE(SendCancel("1").ok());    // CAPACITY_RELEASED

  auto frames = ReadFrames();
  ASSERT_EQ(frames.size(), 2U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[0]).id(), "1");
  EXPECT_EQ(frames[1].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[1]).id(), "2");
}

TEST_F(ProbeLocalSchedulerTest, CachedDepsSkipFetchAndPullImmediately) {
  auto ray = std::make_unique<RecordingFetcher>("ray");
  RecordingFetcher* fetcher = ray.get();
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  std::vector<extensions::DataDependencyFetcherPtr> fetchers;
  fetchers.push_back(std::move(ray));
  object_cache_.Populate(MakeDepRef("ray", "objA"), "payload-A");
  scheduler_ = MakeScheduler(run_task_service, 4, 4, std::move(fetchers));
  ExpectWrites();

  // The byte store is shared: an already-cached dep needs neither a fetch nor
  // DEP_COMPLETED — the direct-admit probe pulls immediately.
  auto deps = google::protobuf::RepeatedPtrField<task::DataRef>{};
  *deps.Add() = MakeDepRef("ray", "objA");
  ASSERT_TRUE(SendProbeWithDeps("1", 2, deps).ok());

  auto frames = ReadFrames();
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[0]).id(), "1");
  EXPECT_TRUE(fetcher->fetched_.empty());
}

TEST_F(ProbeLocalSchedulerTest, CachedDepsPullFromQueueWithoutDepCompleted) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  object_cache_.Populate(MakeDepRef("ray", "objB"), "payload-B");
  scheduler_ = MakeScheduler(run_task_service, 4, 4);
  ExpectWrites();

  ASSERT_TRUE(SendProbe("1", 10).ok()); // admitted + pulled
  auto deps = google::protobuf::RepeatedPtrField<task::DataRef>{};
  *deps.Add() = MakeDepRef("ray", "objB");
  ASSERT_TRUE(SendProbeWithDeps("2", 10, deps).ok()); // enqueued
  ASSERT_TRUE(SendCancel("1").ok());                  // capacity released

  // Deps were already cached on arrival: "2" is ready the moment capacity
  // frees, with no DEP_COMPLETED traffic in between.
  auto frames = ReadFrames();
  ASSERT_EQ(frames.size(), 2U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[0]).id(), "1");
  EXPECT_EQ(frames[1].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[1]).id(), "2");
}

TEST_F(ProbeLocalSchedulerTest, UnknownSourceDepNeverGatesReadiness) {
  // A node without a fetcher for a scheme still runs the task: the ref is
  // fetched on demand by the task handler instead of gating admission.
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = MakeScheduler(run_task_service, 4, 4);
  ExpectWrites();

  ASSERT_TRUE(SendProbe("1", 10).ok()); // admitted + pulled
  auto deps = google::protobuf::RepeatedPtrField<task::DataRef>{};
  *deps.Add() = MakeDepRef("no-such-scheme", "objX");
  ASSERT_TRUE(SendProbeWithDeps("2", 10, deps).ok()); // enqueued
  ASSERT_TRUE(SendCancel("1").ok());                  // capacity released

  auto frames = ReadFrames();
  ASSERT_EQ(frames.size(), 2U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[0]).id(), "1");
  EXPECT_EQ(frames[1].type, io::TlvFrame::kTaskPull);
  EXPECT_EQ(ParsePull(frames[1]).id(), "2");
}

TEST_F(ProbeLocalSchedulerTest, DepCompletedUnknownTaskIsNoop) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = MakeScheduler(run_task_service, 4, 4);
  ExpectWrites();

  ASSERT_TRUE(SendProbe("1", 10).ok()); // admitted + pulled

  // Drain the initial pull, then confirm DEP_COMPLETED for an unknown task does
  // not manufacture a pull: walk() has nothing to serve and must not write.
  auto frames = ReadFrames();
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].type, io::TlvFrame::kTaskPull);

  std::string ghost = "ghost";
  DeliverDepCompleted(ghost);
  EXPECT_TRUE(ReadFrames().empty());
  EXPECT_EQ(admission_->SharedFree("cpu"), 8U);
  EXPECT_EQ(admission_->InFlight("echo"), 1U);
}

TEST_F(ProbeLocalSchedulerTest, ScheduleIsUnimplemented) {
  admission_ = std::make_shared<AdmissionControllerImpl>(*MakePubCaps(), *dispatcher_);
  RunTaskServiceImpl run_task_service(MakeEchoManager(), admission_);
  scheduler_ = MakeScheduler(run_task_service, 1, 1);
  ExpectWrites();

  // The probe entry is a pure wire-protocol counterpart: its node-local
  // Schedule facet is unimplemented (the bundled "default" scheduler is the
  // single local authority). The receiver still resolves — with an error — so
  // a misrouted child can never hang its parent.
  task::Task child;
  child.set_id("child-1");
  child.set_type("echo");
  (*child.mutable_requirements()->mutable_resources())["cpu"] = 16;
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  scheduler_->Schedule(child, std::move(receiver));

  ASSERT_FALSE(log->delivered);
  EXPECT_NE(log->error.find("unimplemented"), std::string::npos) << "error='" << log->error << "'";

  // Nothing was reserved, queued, or pulled: the probe queue machinery is
  // untouched by a misrouted child submission.
  EXPECT_EQ(admission_->SharedFree("cpu"), 18U);
  EXPECT_EQ(admission_->InFlight("echo"), 0U);
}

class StubRunTaskService final : public nodeagent::RunTaskService {
public:
  void RunTask(const task::Task& /*task*/, io::Connection& /*conn*/) override {}
  void RunTask(const task::Task& /*task*/, io::Connection& /*conn*/,
               AdmissionScopePtr /*reserved*/) override {}
  void RunTask(const task::Task& /*task*/, std::unique_ptr<nodeagent::ResultSender> /*sender*/,
               AdmissionScopePtr /*reserved*/) override {}
  [[nodiscard]] auto HasHandler(std::string_view /*type*/) const -> bool override { return false; }
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
  InMemoryObjectCache cache;
  auto router = DataDependencyFetcherRouter::Build(cache, {}).value();

  extensions::StubChildTaskForwarder forwarder;
  const NodeSchedulerDeps deps{*dispatcher, run_task_service, admission, forwarder, *router};

  ProbeLocalSchedulerFactory factory;
  {
    auto config = std::make_unique<extensions::schedulers::probe::ProbeSchedulerConfig>();
    config->set_queue_capacity(0);
    EXPECT_EQ(factory.Create(*config, deps), nullptr);
  }
  {
    auto config = std::make_unique<extensions::schedulers::probe::ProbeSchedulerConfig>();
    config->set_max_concurrent_preallocations(0);
    EXPECT_EQ(factory.Create(*config, deps), nullptr);
  }
  {
    // Absent fields fall back to defaults.
    auto config = std::make_unique<extensions::schedulers::probe::ProbeSchedulerConfig>();
    EXPECT_NE(factory.Create(*config, deps), nullptr);
  }
  {
    auto config = std::make_unique<extensions::schedulers::probe::ProbeSchedulerConfig>();
    config->set_queue_capacity(7);
    config->set_max_concurrent_preallocations(3);
    EXPECT_NE(factory.Create(*config, deps), nullptr);
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
  InMemoryObjectCache cache;
  auto router = DataDependencyFetcherRouter::Build(cache, {}).value();

  extensions::StubChildTaskForwarder forwarder;
  const NodeSchedulerDeps deps{*dispatcher, run_task_service, admission, forwarder, *router};

  task::Task unrelated;
  ProbeLocalSchedulerFactory factory;
  EXPECT_EQ(factory.Create(unrelated, deps), nullptr);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::schedulers::probe