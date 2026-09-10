#include <bit>
#include <chrono>
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

#include "strij/extensions/extension_registry.hh"
#include "gateway/core/node_directory.hh"
#include "gateway/core/result_receiver_storage.hh"
#include "common/core/io/connection.hh"
#include "common/core/io/protocol_parser.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/io/tlv_parser.hh"
#include "common/node/capabilities.pb.h"
#include "common/task/probe.pb.h"
#include "common/task/task.pb.h"
#include "gateway/extensions/schedulers/probe/probe.pb.h"
#include "gateway/extensions/schedulers/probe/probe_scheduler.hh"
#include "strij/extensions/scheduler.hh"
#include "gtest/gtest.h"

namespace strij::gateway::schedulers::probe {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;

class RecordingReceiver final : public gateway::ResultReceiver {
public:
  explicit RecordingReceiver(std::shared_ptr<std::vector<std::string>> errors)
      : errors_{std::move(errors)} {}

  void Deliver(std::span<const std::byte> /*value*/, bool /*is_final*/) override {}
  void DeliverError(std::string_view reason) override { errors_->emplace_back(reason); }

  std::shared_ptr<std::vector<std::string>> errors_;
};

auto MakeReceiver()
    -> std::pair<gateway::ResultReceiverPtr, std::shared_ptr<std::vector<std::string>>> {
  auto errors = std::make_shared<std::vector<std::string>>();
  auto receiver = std::make_unique<RecordingReceiver>(errors);
  return {std::move(receiver), std::move(errors)};
}

auto MakeTask(const std::string& id, const std::string& type) -> task::Task {
  task::Task task;
  task.set_id(id);
  task.set_type(type);
  return task;
}

struct Written {
  event::Completable* conn{nullptr};
  std::vector<std::byte> bytes;
};

// Extracts the single TLV frame carried by one Write() submission.
auto SingleFrame(const std::vector<std::byte>& bytes) -> io::TlvFrame {
  std::vector<io::TlvFrame> out;
  io::TlvParser parser([&out](io::TlvFrame frame) { out.push_back(std::move(frame)); });
  auto buffer = parser.GetReadBuffer();
  std::memcpy(buffer.data(), bytes.data(), bytes.size());
  parser.OnData(bytes.size());
  return out.front();
}

auto ProbeId(const io::TlvFrame& frame) -> std::string {
  task::TaskProbe probe;
  probe.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                       static_cast<int>(frame.value.size()));
  return probe.id();
}

bool HasType(const std::vector<Written>& writes, io::Connection* conn, uint8_t type) {
  return std::any_of(writes.begin(), writes.end(), [&](const Written& w) {
    return w.conn == conn && SingleFrame(w.bytes).type_id == type;
  });
}

class FakeClock {
public:
  auto Now() -> std::chrono::steady_clock::time_point { return now_; }
  void Advance(std::chrono::milliseconds ms) { now_ += ms; }
  auto AsClock() const -> ProbeScheduler::Clock { return [this] { return now_; }; }

private:
  std::chrono::steady_clock::time_point now_{std::chrono::steady_clock::now()};
};

class ProbeSchedulerTest : public ::testing::Test {
protected:
  std::shared_ptr<event::MockDispatcher> dispatcher_{std::make_shared<event::MockDispatcher>()};
  gateway::ResultReceiverStorageImpl storage_;

  auto MakeConnectedDirectory(std::initializer_list<std::string> ids)
      -> std::unique_ptr<gateway::NodeDirectory> {
    auto directory = std::make_unique<gateway::NodeDirectoryImpl>(
        dispatcher_,
        [](io::Connection&) -> io::ProtocolParserPtr {
          return std::make_unique<io::TrivialParser>();
        },
        storage_);
    EXPECT_CALL(*dispatcher_, PrepareConnect(_, _, _, _, _)).WillRepeatedly(Return());
    EXPECT_CALL(*dispatcher_, PrepareRead(_, _, _, _, _)).WillRepeatedly(Return());
    for (const auto& node_id : ids) {
      directory->AddNode(node_id, "10.0.0.1:9090");
    }
    for (const auto& node_id : ids) {
      directory->GetNode(node_id)->HandleCompletion(0, 0, 0);
    }
    return directory;
  }

  void RecordWrites(std::vector<Written>* writes) {
    EXPECT_CALL(*dispatcher_, PrepareWrite(_, _, _, _, _))
        .WillRepeatedly([writes](event::Completable* io, uint8_t /*tag*/, int /*fd*/,
                                 std::span<const std::byte> buf, off_t /*offset*/) {
          writes->push_back({io, std::vector<std::byte>(buf.begin(), buf.end())});
        });
  }

  // Simulate io_uring write completions on a connection so that its write queue
  // drains and the next Write() call triggers a fresh PrepareWrite.
  static void SimulateDrainedWrites(gateway::NodeDirectory& dir,
                                    const std::string& node_id) {
    auto* conn = dir.GetNode(node_id)->GetConnection();
    conn->HandleCompletion(1 /*kWrite*/, 65536, 0);
  }

  // Raw protobuf payload (no TLV framing) for HandleFrame's frame.value field.
  static auto MakeRawPullPayload(const std::string& id) -> std::vector<std::byte> {
    task::TaskPull pull;
    pull.set_id(id);
    std::string serialized;
    pull.SerializeToString(&serialized);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* data = reinterpret_cast<const std::byte*>(serialized.data());
    return std::vector<std::byte>(data, data + serialized.size());
  }

  static auto MakeRawDeclinePayload(const std::string& id, const std::string& reason)
      -> std::vector<std::byte> {
    task::TaskDecline decline;
    decline.set_id(id);
    decline.set_reason(reason);
    std::string serialized;
    decline.SerializeToString(&serialized);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* data = reinterpret_cast<const std::byte*>(serialized.data());
    return std::vector<std::byte>(data, data + serialized.size());
  }
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(ProbeSchedulerTest, ProbeCarriesTaskDeps) {
  auto directory = MakeConnectedDirectory({"A"});
  FakeClock clock;
  ProbeScheduler scheduler(*directory, storage_, dispatcher_, /*candidate_count=*/1,
                           std::chrono::milliseconds(1000), clock.AsClock());

  std::vector<Written> writes;
  RecordWrites(&writes);

  task::Task task;
  task.set_id("1");
  task.set_type("echo");
  auto* dep = task.mutable_deps()->Add();
  dep->set_source("ray");
  dep->set_key("obj_abc");
  dep->set_sha("deadbeef");

  auto receiver = MakeReceiver();
  scheduler.Schedule(task, std::move(receiver.first));

  ASSERT_EQ(writes.size(), 1U);
  const io::TlvFrame frame = SingleFrame(writes[0].bytes);
  EXPECT_EQ(frame.type_id, io::TlvFrame::kTaskProbe);

  task::TaskProbe probe;
  ASSERT_TRUE(probe.ParseFromArray(std::bit_cast<const char*>(frame.value.data()),
                                   static_cast<int>(frame.value.size())));
  ASSERT_EQ(probe.deps_size(), 1);
  EXPECT_EQ(probe.deps(0).source(), "ray");
  EXPECT_EQ(probe.deps(0).key(), "obj_abc");
}

TEST_F(ProbeSchedulerTest, ClampsCandidatesToAvailableNodes) {
  auto directory = MakeConnectedDirectory({"A"});
  FakeClock clock;
  ProbeScheduler scheduler(*directory, storage_, dispatcher_, /*candidate_count=*/5,
                           std::chrono::milliseconds(1000), clock.AsClock());

  std::vector<Written> writes;
  RecordWrites(&writes);

  auto receiver = MakeReceiver();
  scheduler.Schedule(MakeTask("1", "echo"), std::move(receiver.first));

  ASSERT_EQ(writes.size(), 1U);
  const io::TlvFrame frame = SingleFrame(writes[0].bytes);
  EXPECT_EQ(frame.type_id, io::TlvFrame::kTaskProbe);
  EXPECT_EQ(ProbeId(frame), "1");
  EXPECT_EQ(storage_.Get("1"), nullptr);
  EXPECT_TRUE(receiver.second->empty());
}

TEST_F(ProbeSchedulerTest, DeterministicPerTaskSampling) {
  auto directory = MakeConnectedDirectory({"A", "B"});
  FakeClock clock;
  ProbeScheduler scheduler(*directory, storage_, dispatcher_, /*candidate_count=*/2,
                           std::chrono::milliseconds(1000), clock.AsClock());

  std::vector<Written> writes;
  RecordWrites(&writes);

  // Schedule the same task id twice (with a sweep in between to clear the
  // first pending entry) and verify that the probe order is identical.
  scheduler.Schedule(MakeTask("same", "echo"), MakeReceiver().first);
  ASSERT_EQ(writes.size(), 2U);
  const auto* first_a = writes[0].conn;
  const auto* first_b = writes[1].conn;

  // Clear pending by advancing past deadline and sweeping.
  clock.Advance(std::chrono::milliseconds(2000));
  scheduler.SweepExpired();
  // Drain probe frames first (cancel frames land behind them), then drain the
  // cancel frames that the sweep enqueued.
  SimulateDrainedWrites(*directory, "A");
  SimulateDrainedWrites(*directory, "B");
  SimulateDrainedWrites(*directory, "A");
  SimulateDrainedWrites(*directory, "B");
  writes.clear();

  scheduler.Schedule(MakeTask("same", "echo"), MakeReceiver().first);
  ASSERT_EQ(writes.size(), 2U);
  EXPECT_EQ(writes[0].conn, first_a);
  EXPECT_EQ(writes[1].conn, first_b);
}

TEST_F(ProbeSchedulerTest, NoEligibleNodesErrorsImmediately) {
  auto directory = MakeConnectedDirectory({});
  FakeClock clock;
  ProbeScheduler scheduler(*directory, storage_, dispatcher_, /*candidate_count=*/2,
                           std::chrono::milliseconds(1000), clock.AsClock());

  std::vector<Written> writes;
  RecordWrites(&writes);

  auto receiver = MakeReceiver();
  scheduler.Schedule(MakeTask("1", "echo"), std::move(receiver.first));

  ASSERT_EQ(receiver.second->size(), 1U);
  EXPECT_NE(receiver.second->at(0).find("no probe-eligible nodes"), std::string::npos);
  EXPECT_TRUE(writes.empty());
  EXPECT_EQ(storage_.Get("1"), nullptr);
}

TEST_F(ProbeSchedulerTest, FirstPullWinsAndLoserIsCancelled) {
  auto directory = MakeConnectedDirectory({"A", "B"});
  FakeClock clock;
  ProbeScheduler scheduler(*directory, storage_, dispatcher_, /*candidate_count=*/2,
                           std::chrono::milliseconds(1000), clock.AsClock());

  std::vector<Written> writes;
  RecordWrites(&writes);

  auto receiver = MakeReceiver();
  scheduler.Schedule(MakeTask("1", "echo"), std::move(receiver.first));
  ASSERT_EQ(writes.size(), 2U); // probes to A and B

  // Drain write queues so subsequent writes trigger fresh PrepareWrite calls.
  SimulateDrainedWrites(*directory, "A");
  SimulateDrainedWrites(*directory, "B");

  io::Connection* a_conn = directory->GetNode("A")->GetConnection();
  io::Connection* b_conn = directory->GetNode("B")->GetConnection();

  // Node A pulls first: it wins the grant; B is cancelled.
  ASSERT_TRUE(
      scheduler.HandleFrame({.type_id = io::TlvFrame::kTaskPull, .value = MakeRawPullPayload("1")},
                            *a_conn)
          .ok());

  ASSERT_GE(writes.size(), 4U);
  EXPECT_TRUE(HasType(writes, a_conn, io::TlvFrame::kTaskGrant));
  EXPECT_TRUE(HasType(writes, b_conn, io::TlvFrame::kTaskProbeCancel));

  // The receiver transferred into storage for the winner's node id.
  EXPECT_NE(storage_.Get("1"), nullptr);

  // A late pull (from B) after the round resolved only revokes: no second grant.
  SimulateDrainedWrites(*directory, "B");
  const size_t before = writes.size();
  ASSERT_TRUE(
      scheduler.HandleFrame({.type_id = io::TlvFrame::kTaskPull, .value = MakeRawPullPayload("1")},
                            *b_conn)
          .ok());
  ASSERT_EQ(writes.size(), before + 1U);
  EXPECT_EQ(SingleFrame(writes.back().bytes).type_id, io::TlvFrame::kTaskProbeCancel);
  EXPECT_EQ(writes.back().conn, b_conn);
}

TEST_F(ProbeSchedulerTest, PullFromUnprobedNodeIsRevoked) {
  auto directory = MakeConnectedDirectory({"A", "B"});
  FakeClock clock;
  ProbeScheduler scheduler(*directory, storage_, dispatcher_, /*candidate_count=*/1,
                           std::chrono::milliseconds(1000), clock.AsClock());

  std::vector<Written> writes;
  RecordWrites(&writes);

  // candidate_count=1: only one node is probed. A pull from the OTHER node must
  // never be granted.
  scheduler.Schedule(MakeTask("1", "echo"), MakeReceiver().first);
  ASSERT_EQ(writes.size(), 1U);

  io::Connection* a_conn = directory->GetNode("A")->GetConnection();
  io::Connection* b_conn = directory->GetNode("B")->GetConnection();

  // Identify which node was NOT probed (the one whose connection has no writes).
  io::Connection* unprobed_conn = (writes[0].conn == a_conn) ? b_conn : a_conn;
  SimulateDrainedWrites(*directory,
                        (writes[0].conn == a_conn) ? "A" : "B");

  ASSERT_TRUE(
      scheduler.HandleFrame(
          {.type_id = io::TlvFrame::kTaskPull, .value = MakeRawPullPayload("1")},
          *unprobed_conn)
          .ok());

  ASSERT_EQ(writes.size(), 2U);
  EXPECT_EQ(SingleFrame(writes[1].bytes).type_id, io::TlvFrame::kTaskProbeCancel);
  EXPECT_EQ(writes[1].conn, unprobed_conn);
  EXPECT_EQ(storage_.Get("1"), nullptr);
}

TEST_F(ProbeSchedulerTest, AllDeclinesErrorReceiversImmediately) {
  auto directory = MakeConnectedDirectory({"A", "B"});
  FakeClock clock;
  ProbeScheduler scheduler(*directory, storage_, dispatcher_, /*candidate_count=*/2,
                           std::chrono::milliseconds(1000), clock.AsClock());

  std::vector<Written> writes;
  RecordWrites(&writes);

  auto receiver = MakeReceiver();
  scheduler.Schedule(MakeTask("1", "echo"), std::move(receiver.first));

  // Drain write queues so decline-frame PrepareWrite calls are visible.
  SimulateDrainedWrites(*directory, "A");
  SimulateDrainedWrites(*directory, "B");

  io::Connection* a_conn = directory->GetNode("A")->GetConnection();
  io::Connection* b_conn = directory->GetNode("B")->GetConnection();
  ASSERT_TRUE(
      scheduler.HandleFrame({.type_id = io::TlvFrame::kTaskDecline,
                             .value = MakeRawDeclinePayload("1", "busy")},
                            *a_conn)
          .ok());
  EXPECT_TRUE(receiver.second->empty());

  ASSERT_TRUE(
      scheduler.HandleFrame({.type_id = io::TlvFrame::kTaskDecline,
                             .value = MakeRawDeclinePayload("1", "busy")},
                            *b_conn)
          .ok());
  ASSERT_EQ(receiver.second->size(), 1U);
  EXPECT_NE(receiver.second->at(0).find("busy"), std::string::npos);
  EXPECT_EQ(storage_.Get("1"), nullptr);
}

TEST_F(ProbeSchedulerTest, DeclinesAfterResolutionAreIgnored) {
  auto directory = MakeConnectedDirectory({"A"});
  FakeClock clock;
  ProbeScheduler scheduler(*directory, storage_, dispatcher_, /*candidate_count=*/1,
                           std::chrono::milliseconds(1000), clock.AsClock());

  std::vector<Written> writes;
  RecordWrites(&writes);

  scheduler.Schedule(MakeTask("1", "echo"), MakeReceiver().first);

  // Drain so the pull-triggered grant/cancel writes are visible.
  SimulateDrainedWrites(*directory, "A");

  io::Connection* a_conn = directory->GetNode("A")->GetConnection();
  ASSERT_TRUE(
      scheduler.HandleFrame({.type_id = io::TlvFrame::kTaskPull, .value = MakeRawPullPayload("1")},
                            *a_conn)
          .ok());

  // Drain the grant write before sending the decline.
  SimulateDrainedWrites(*directory, "A");

  const size_t before = writes.size();
  ASSERT_TRUE(
      scheduler.HandleFrame({.type_id = io::TlvFrame::kTaskDecline,
                             .value = MakeRawDeclinePayload("1", "late")},
                            *a_conn)
          .ok());
  EXPECT_EQ(writes.size(), before);
}

TEST_F(ProbeSchedulerTest, DeadlineExpiryErrorsAndCancelsOutstanding) {
  auto directory = MakeConnectedDirectory({"A", "B"});
  FakeClock clock;
  ProbeScheduler scheduler(*directory, storage_, dispatcher_, /*candidate_count=*/2,
                           std::chrono::milliseconds(1000), clock.AsClock());

  std::vector<Written> writes;
  RecordWrites(&writes);

  auto receiver = MakeReceiver();
  scheduler.Schedule(MakeTask("1", "echo"), std::move(receiver.first));
  EXPECT_TRUE(receiver.second->empty());

  // Drain probe writes so that cancel-write PrepareWrite calls from the sweep
  // are visible in the recorder.
  SimulateDrainedWrites(*directory, "A");
  SimulateDrainedWrites(*directory, "B");

  clock.Advance(std::chrono::milliseconds(2000));
  scheduler.SweepExpired();

  ASSERT_EQ(receiver.second->size(), 1U);
  EXPECT_NE(receiver.second->at(0).find("probe deadline"), std::string::npos);
  EXPECT_EQ(storage_.Get("1"), nullptr);

  const size_t cancels = static_cast<size_t>(std::count_if(
      writes.begin(), writes.end(), [](const Written& w) {
        return SingleFrame(w.bytes).type_id == io::TlvFrame::kTaskProbeCancel;
      }));
  EXPECT_EQ(cancels, 2U);
}

TEST_F(ProbeSchedulerTest, ReceiversResolveOnlyOnceAcrossAllPaths) {
  auto directory = MakeConnectedDirectory({"A"});
  FakeClock clock;
  ProbeScheduler scheduler(*directory, storage_, dispatcher_, /*candidate_count=*/1,
                           std::chrono::milliseconds(1000), clock.AsClock());

  std::vector<Written> writes;
  RecordWrites(&writes);

  auto receiver = MakeReceiver();
  scheduler.Schedule(MakeTask("1", "echo"), std::move(receiver.first));

  // Deadline expires → resolves with an error.
  clock.Advance(std::chrono::milliseconds(2000));
  scheduler.SweepExpired();
  ASSERT_EQ(receiver.second->size(), 1U);

  // Drain the cancel writes from the sweep.
  SimulateDrainedWrites(*directory, "A");

  // A pull arriving after resolution revokes but must not re-run resolution.
  io::Connection* a_conn = directory->GetNode("A")->GetConnection();
  ASSERT_TRUE(
      scheduler.HandleFrame({.type_id = io::TlvFrame::kTaskPull, .value = MakeRawPullPayload("1")},
                            *a_conn)
          .ok());
  EXPECT_EQ(receiver.second->size(), 1U);
}

TEST_F(ProbeSchedulerTest, FactoryValidatesCandidateCount) {
  auto directory = MakeConnectedDirectory({"A"});
  extensions::MockGatewayFactoryContext context;
  ON_CALL(context, NodeDirectory()).WillByDefault(ReturnRef(*directory));
  ON_CALL(context, ResultReceiverStorage()).WillByDefault(ReturnRef(storage_));
  ON_CALL(context, SharedDispatcher()).WillByDefault(Return(dispatcher_));

  ProbeSchedulerFactory factory;
  EXPECT_EQ(factory.Name(), "probe");

  auto config = std::make_unique<extensions::schedulers::probe::ProbeRoleSchedulerConfig>();
  config->set_candidate_count(0);
  EXPECT_EQ(factory.Create(*config, context), nullptr);

  config = std::make_unique<extensions::schedulers::probe::ProbeRoleSchedulerConfig>();
  EXPECT_NE(factory.Create(*config, context), nullptr);

  config = std::make_unique<extensions::schedulers::probe::ProbeRoleSchedulerConfig>();
  config->set_candidate_count(3);
  config->set_probe_deadline_ms(500);
  EXPECT_NE(factory.Create(*config, context), nullptr);
}

TEST_F(ProbeSchedulerTest, FactoryRejectsWrongConfigType) {
  auto directory = MakeConnectedDirectory({"A"});
  extensions::MockGatewayFactoryContext context;
  ON_CALL(context, NodeDirectory()).WillByDefault(ReturnRef(*directory));
  ON_CALL(context, ResultReceiverStorage()).WillByDefault(ReturnRef(storage_));

  task::Task unrelated;
  ProbeSchedulerFactory factory;
  EXPECT_EQ(factory.Create(unrelated, context), nullptr);
}

TEST_F(ProbeSchedulerTest, UnknownFrameTypeIsNotFound) {
  auto directory = MakeConnectedDirectory({"A"});
  FakeClock clock;
  ProbeScheduler scheduler(*directory, storage_, dispatcher_, /*candidate_count=*/2,
                           std::chrono::milliseconds(1000), clock.AsClock());

  task::Task task;
  task.set_id("x");
  std::string serialized;
  task.SerializeToString(&serialized);
  const absl::Status status = scheduler.HandleFrame(
      {.type_id = io::TlvFrame::kTaskProbe, .value = std::as_bytes(std::span(serialized))},
      *directory->GetNode("A")->GetConnection());
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::gateway::schedulers::probe