#include <bit>
#include <cstddef>
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
#include "common/task/task.pb.h"
#include "gateway/extensions/schedulers/round_robin/round_robin_scheduler.hh"
#include "strij/extensions/scheduler.hh"
#include "gtest/gtest.h"

namespace strij::gateway::schedulers {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;

class RecordingReceiver final : public gateway::ResultReceiver {
public:
  explicit RecordingReceiver(std::shared_ptr<std::vector<std::string>> errors)
      : errors_{std::move(errors)} {}

  void Deliver(std::span<const std::byte> /*value*/, bool /*is_final*/) override {}
  void DeliverError(std::string_view reason) override { errors_->emplace_back(reason); }

  std::shared_ptr<std::vector<std::string>> errors_;
};

auto MakeReceiver() -> std::pair<gateway::ResultReceiverPtr, std::shared_ptr<std::vector<std::string>>> {
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

class RoundRobinSchedulerTest : public ::testing::Test {
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
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(RoundRobinSchedulerTest, RotatesOverAvailableNodes) {
  auto directory = MakeConnectedDirectory({"A", "B"});
  RoundRobinScheduler scheduler(*directory, storage_);

  auto* a_conn = static_cast<event::Completable*>(directory->GetNode("A")->GetConnection());
  auto* b_conn = static_cast<event::Completable*>(directory->GetNode("B")->GetConnection());

  // Drain the write queue after each issued write to mimic a real event loop
  // completing the submission.
  std::vector<event::Completable*> order;
  EXPECT_CALL(*dispatcher_, PrepareWrite(_, _, _, _, _))
      .WillRepeatedly([&order](event::Completable* io, uint8_t tag, int /*fd*/,
                               std::span<const std::byte> buf, off_t /*offset*/) {
        order.push_back(io);
        io->HandleCompletion(tag, static_cast<int>(buf.size()), 0);
      });

  scheduler.Schedule(MakeTask("1", "echo"), MakeReceiver().first);
  scheduler.Schedule(MakeTask("2", "echo"), MakeReceiver().first);
  scheduler.Schedule(MakeTask("3", "echo"), MakeReceiver().first);

  ASSERT_EQ(order.size(), 3U);
  EXPECT_EQ(order[0], a_conn);
  EXPECT_EQ(order[1], b_conn);
  EXPECT_EQ(order[2], a_conn);
}

TEST_F(RoundRobinSchedulerTest, WritesTaskAsSubmissionFrame) {
  auto directory = MakeConnectedDirectory({"A"});
  RoundRobinScheduler scheduler(*directory, storage_);

  std::span<const std::byte> written;
  EXPECT_CALL(*dispatcher_, PrepareWrite(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<3>(&written), Return()));

  scheduler.Schedule(MakeTask("7", "echo"), MakeReceiver().first);

  std::vector<io::TlvFrame> frames;
  io::TlvParser parser([&frames](io::TlvFrame frame) { frames.push_back(std::move(frame)); });
  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), written.data(), written.size());
  parser.OnData(written.size());
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].type_id, io::TlvFrame::kTaskSubmission);

  task::Task task;
  ASSERT_TRUE(task.ParseFromArray(std::bit_cast<const char*>(frames[0].value.data()),
                                  static_cast<int>(frames[0].value.size())));
  EXPECT_EQ(task.id(), "7");
  EXPECT_EQ(task.type(), "echo");

  EXPECT_NE(storage_.Get("7"), nullptr);
}

TEST_F(RoundRobinSchedulerTest, DeliversErrorWhenNoNodeAvailable) {
  auto directory = MakeConnectedDirectory({});
  RoundRobinScheduler scheduler(*directory, storage_);
  EXPECT_CALL(*dispatcher_, PrepareWrite(_, _, _, _, _)).Times(0);

  auto receiver = MakeReceiver();
  scheduler.Schedule(MakeTask("1", "echo"), std::move(receiver.first));

  ASSERT_EQ(receiver.second->size(), 1U);
  EXPECT_NE(receiver.second->at(0).find("no available node"), std::string::npos);
  EXPECT_EQ(storage_.Get("1"), nullptr);
}

TEST_F(RoundRobinSchedulerTest, ExcludesNodesNotAdvertisingRequiredProtocol) {
  auto directory = MakeConnectedDirectory({"push", "probe", "quiet"});
  auto* push_node = directory->GetNode("push");
  auto* probe_node = directory->GetNode("probe");

  node::NodeCapabilities push_caps;
  push_caps.add_scheduling_protocols()->set_name("push");
  push_node->StoreCapabilities(std::move(push_caps));

  node::NodeCapabilities probe_caps;
  probe_caps.add_scheduling_protocols()->set_name("probe");
  probe_node->StoreCapabilities(std::move(probe_caps));

  RoundRobinScheduler scheduler(*directory, storage_);

  event::Completable* first = nullptr;
  event::Completable* second = nullptr;
  EXPECT_CALL(*dispatcher_, PrepareWrite(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&first), Return()))
      .WillOnce(DoAll(SaveArg<0>(&second), Return()));

  scheduler.Schedule(MakeTask("1", "echo"), MakeReceiver().first);
  scheduler.Schedule(MakeTask("2", "echo"), MakeReceiver().first);

  auto* probe_conn = static_cast<event::Completable*>(probe_node->GetConnection());
  EXPECT_NE(first, probe_conn);
  EXPECT_NE(second, probe_conn);
}

TEST_F(RoundRobinSchedulerTest, FactoryIsRegisteredAndCreatesScheduler) {
  auto* factory = extensions::Registry<gateway::GatewaySchedulerFactory>::instance()
                      .GetFactory("round_robin");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->Name(), "round_robin");

  auto directory = MakeConnectedDirectory({"A"});
  extensions::MockGatewayFactoryContext context;
  ON_CALL(context, NodeDirectory()).WillByDefault(ReturnRef(*directory));
  ON_CALL(context, ResultReceiverStorage()).WillByDefault(ReturnRef(storage_));

  auto config = factory->CreateEmptyConfigProto();
  auto scheduler = factory->Create(*config, context);
  ASSERT_NE(scheduler, nullptr);
  EXPECT_EQ(scheduler->RequiredProtocol(), "push");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::gateway::schedulers