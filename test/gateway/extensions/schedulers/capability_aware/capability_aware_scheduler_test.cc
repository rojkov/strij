#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"
#include "test/mocks/extensions/extensions_mocks.hh"

#include "strij/extensions/extension_registry.hh"
#include "gateway/core/node_directory.hh"
#include "gateway/core/result_receiver_storage.hh"
#include "common/core/io/connection.hh"
#include "common/core/io/protocol_parser.hh"
#include "common/node/capabilities.pb.h"
#include "common/task/task.pb.h"
#include "gateway/extensions/schedulers/capability_aware/capability_aware_scheduler.hh"
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

class CapabilityAwareSchedulerTest : public ::testing::Test {
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

  static void AddProtocol(node::NodeCapabilities& caps, std::string_view name) {
    caps.add_scheduling_protocols()->set_name(std::string(name));
  }

  static void AddPool(node::NodeCapabilities& caps, const std::string& name, uint64_t total) {
    caps.add_pools()->set_name(name);
    caps.mutable_pools()->rbegin()->set_total(total);
  }

  static void AddHandler(node::NodeCapabilities& caps, const std::string& task_type,
                         uint64_t concurrency) {
    auto* handler = caps.add_handlers();
    handler->set_task_type(task_type);
    handler->set_concurrency(concurrency);
  }

  static void SetTypeInFlight(node::NodeState& state, const std::string& task_type,
                              uint64_t in_flight) {
    auto* usage = state.add_type_usage();
    usage->set_task_type(task_type);
    usage->set_in_flight(in_flight);
  }

  static void SetNodeWideInFlight(node::NodeState& state, uint64_t in_flight) {
    state.set_in_flight(in_flight);
  }

  static auto MakePinnedTask(const std::string& id, const std::string& type,
                             std::initializer_list<std::pair<const std::string, uint64_t>> resources)
      -> task::Task {
    task::Task task;
    task.set_id(id);
    task.set_type(type);
    for (const auto& [pool, amount] : resources) {
      (*task.mutable_requirements()->mutable_resources())[pool] = amount;
    }
    return task;
  }
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(CapabilityAwareSchedulerTest, ExcludesNodeWithoutRequiredPoolCapacity) {
  auto directory = MakeConnectedDirectory({"free", "quota"});

  node::NodeCapabilities free_caps;
  AddProtocol(free_caps, "push");
  AddPool(free_caps, "cpu", 4);
  directory->GetNode("free")->StoreCapabilities(std::move(free_caps));

  node::NodeCapabilities quota_caps;
  AddProtocol(quota_caps, "push");
  AddPool(quota_caps, "cpu", 2);
  directory->GetNode("quota")->StoreCapabilities(std::move(quota_caps));

  CapabilityAwareScheduler scheduler(*directory, storage_);

  auto* expected_conn = directory->GetNode("free")->GetConnection();
  event::Completable* written_to = nullptr;
  EXPECT_CALL(*dispatcher_, PrepareWrite(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&written_to), Return()));

  scheduler.Schedule(MakePinnedTask("1", "echo", {{"cpu", 4}}),
                     MakeReceiver().first);
  EXPECT_EQ(written_to, static_cast<event::Completable*>(expected_conn));
}

TEST_F(CapabilityAwareSchedulerTest, ExcludesNodeWithoutHandlerForTaskType) {
  auto directory = MakeConnectedDirectory({"echo_node", "cv_node"});

  node::NodeCapabilities echo_caps;
  AddProtocol(echo_caps, "push");
  AddHandler(echo_caps, "echo", 0);
  directory->GetNode("echo_node")->StoreCapabilities(std::move(echo_caps));

  node::NodeCapabilities cv_caps;
  AddProtocol(cv_caps, "push");
  AddHandler(cv_caps, "cv", 0);
  directory->GetNode("cv_node")->StoreCapabilities(std::move(cv_caps));

  CapabilityAwareScheduler scheduler(*directory, storage_);

  auto* expected_conn = directory->GetNode("cv_node")->GetConnection();
  event::Completable* written_to = nullptr;
  EXPECT_CALL(*dispatcher_, PrepareWrite(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&written_to), Return()));

  scheduler.Schedule(MakePinnedTask("1", "cv", {}), MakeReceiver().first);
  EXPECT_EQ(written_to, static_cast<event::Completable*>(expected_conn));
}

TEST_F(CapabilityAwareSchedulerTest, ChoosesLeastLoadedNodeByConcurrencyRatio) {
  auto directory = MakeConnectedDirectory({"light", "loaded"});

  node::NodeCapabilities light_caps;
  AddProtocol(light_caps, "push");
  AddHandler(light_caps, "echo", 10);
  directory->GetNode("light")->StoreCapabilities(std::move(light_caps));

  node::NodeState light_state;
  SetTypeInFlight(light_state, "echo", 1);
  directory->GetNode("light")->UpdateState(std::move(light_state));

  node::NodeCapabilities loaded_caps;
  AddProtocol(loaded_caps, "push");
  AddHandler(loaded_caps, "echo", 10);
  directory->GetNode("loaded")->StoreCapabilities(std::move(loaded_caps));

  node::NodeState loaded_state;
  SetTypeInFlight(loaded_state, "echo", 5);
  directory->GetNode("loaded")->UpdateState(std::move(loaded_state));

  CapabilityAwareScheduler scheduler(*directory, storage_);

  auto* expected_conn = directory->GetNode("light")->GetConnection();
  event::Completable* written_to = nullptr;
  EXPECT_CALL(*dispatcher_, PrepareWrite(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&written_to), Return()));

  scheduler.Schedule(MakePinnedTask("1", "echo", {}), MakeReceiver().first);
  EXPECT_EQ(written_to, static_cast<event::Completable*>(expected_conn));
}

TEST_F(CapabilityAwareSchedulerTest, TieBreaksByNodeWideInFlightCount) {
  auto directory = MakeConnectedDirectory({"busy", "idle"});

  node::NodeCapabilities busy_caps;
  AddProtocol(busy_caps, "push");
  AddHandler(busy_caps, "echo", 0);
  directory->GetNode("busy")->StoreCapabilities(std::move(busy_caps));

  node::NodeState busy_state;
  SetNodeWideInFlight(busy_state, 100);
  directory->GetNode("busy")->UpdateState(std::move(busy_state));

  node::NodeCapabilities idle_caps;
  AddProtocol(idle_caps, "push");
  AddHandler(idle_caps, "echo", 0);
  directory->GetNode("idle")->StoreCapabilities(std::move(idle_caps));

  node::NodeState idle_state;
  SetNodeWideInFlight(idle_state, 2);
  directory->GetNode("idle")->UpdateState(std::move(idle_state));

  CapabilityAwareScheduler scheduler(*directory, storage_);

  auto* expected_conn = directory->GetNode("idle")->GetConnection();
  event::Completable* written_to = nullptr;
  EXPECT_CALL(*dispatcher_, PrepareWrite(_, _, _, _, _))
      .WillOnce(DoAll(SaveArg<0>(&written_to), Return()));

  scheduler.Schedule(MakePinnedTask("1", "echo", {}), MakeReceiver().first);
  EXPECT_EQ(written_to, static_cast<event::Completable*>(expected_conn));
}

TEST_F(CapabilityAwareSchedulerTest, DeliversErrorWhenNoEligibleNode) {
  auto directory = MakeConnectedDirectory({"saturated"});

  node::NodeCapabilities caps;
  AddProtocol(caps, "push");
  AddHandler(caps, "echo", 1);
  directory->GetNode("saturated")->StoreCapabilities(std::move(caps));

  node::NodeState state;
  SetTypeInFlight(state, "echo", 1);
  directory->GetNode("saturated")->UpdateState(std::move(state));

  CapabilityAwareScheduler scheduler(*directory, storage_);
  EXPECT_CALL(*dispatcher_, PrepareWrite(_, _, _, _, _)).Times(0);

  auto receiver = MakeReceiver();
  scheduler.Schedule(MakePinnedTask("1", "echo", {}), std::move(receiver.first));

  ASSERT_EQ(receiver.second->size(), 1U);
  EXPECT_NE(receiver.second->at(0).find("no eligible node"), std::string::npos);
  EXPECT_EQ(storage_.Get("1"), nullptr);
}

TEST_F(CapabilityAwareSchedulerTest, FactoryIsRegisteredAndCreatesScheduler) {
  auto* factory = extensions::Registry<gateway::GatewaySchedulerFactory>::instance()
                      .GetFactory("capability_aware");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->Name(), "capability_aware");

  auto directory = MakeConnectedDirectory({"node"});
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