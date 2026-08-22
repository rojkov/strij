#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "core/extensions/extension_registry.hh"
#include "core/gateway/node.hh"
#include "core/gateway/node_directory.hh"
#include "core/gateway/result_receiver_storage.hh"
#include "core/io/protocol_parser.hh"
#include "core/node/capabilities.pb.h"
#include "core/task/task.pb.h"
#include "extensions/schedulers/capability_aware/capability_aware_scheduler.hh"
#include "extensions/schedulers/scheduler.hh"
#include "google/protobuf/map.h"
#include "gtest/gtest.h"

namespace strij::extensions::schedulers {
namespace {

class CapabilityAwareSchedulerTest : public ::testing::Test {
protected:
  std::shared_ptr<event::MockDispatcher> dispatcher_{std::make_shared<event::MockDispatcher>()};
  gateway::ResultReceiverStorage storage_;

  auto MakeConnectedDirectory(std::initializer_list<std::string> ids)
      -> std::unique_ptr<gateway::NodeDirectory> {
    auto directory = std::make_unique<gateway::NodeDirectory>(
        dispatcher_,
        [](io::Connection&) -> io::ProtocolParserPtr {
          return std::make_unique<io::TrivialParser>();
        },
        storage_);
    EXPECT_CALL(*dispatcher_, PrepareConnect(::testing::_, ::testing::_, ::testing::_, ::testing::_,
                                             ::testing::_))
        .WillRepeatedly(::testing::Return());
    for (const auto& id : ids) {
      directory->AddNode(id, "10.0.0.1:9090");
    }
    for (const auto& id : ids) {
      directory->GetNode(id)->HandleCompletion(0, 0, 0);
    }
    return directory;
  }

  void AddProtocol(node::NodeCapabilities* caps, std::string_view name) {
    caps->add_scheduling_protocols()->set_name(name);
  }

  void AddPool(node::NodeCapabilities* caps, std::string_view name, uint64_t total) {
    auto* pool = caps->add_pools();
    pool->set_name(name);
    pool->set_total(total);
  }

  void AddReservation(node::NodeCapabilities* caps, std::string_view task_type,
                      std::string_view pool, uint64_t amount) {
    auto* reservation = caps->add_reservations();
    reservation->set_task_type(task_type);
    reservation->set_pool(pool);
    reservation->set_amount(amount);
  }

  void AddHandler(node::NodeCapabilities* caps, std::string_view task_type, uint64_t concurrency) {
    auto* handler = caps->add_handlers();
    handler->set_task_type(task_type);
    handler->set_concurrency(concurrency);
  }

  void SetPoolInUse(node::NodeState* state, std::string_view name, uint64_t in_use) {
    auto* usage = state->add_pools();
    usage->set_pool(name);
    usage->set_in_use(in_use);
  }

  void SetTypeInFlight(node::NodeState* state, std::string_view task_type, uint64_t in_flight) {
    auto* usage = state->add_type_usage();
    usage->set_task_type(task_type);
    usage->set_in_flight(in_flight);
  }

  // Builds an offer that stays alive for the duration of the test.
  auto MakeOffer(std::string task_type,
                 std::initializer_list<std::pair<const std::string, uint64_t>> resources)
      -> TaskOffer {
    task_.set_type(std::move(task_type));
    requirements_.mutable_resources()->clear();
    for (const auto& [pool, amount] : resources) {
      (*requirements_.mutable_resources())[pool] = amount;
    }
    return TaskOffer{.task = &task_, .requirements = &requirements_};
  }

private:
  task::Task task_;
  node::ResourceRequirements requirements_;
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(CapabilityAwareSchedulerTest, ExcludesNodeWithoutRequiredPoolCapacity) {
  auto directory = MakeConnectedDirectory({"full", "free"});
  auto* full = directory->GetNode("full");
  auto* free = directory->GetNode("free");

  node::NodeCapabilities full_caps;
  AddProtocol(&full_caps, "push");
  AddPool(&full_caps, "gpu.h100", 1);
  full->StoreCapabilities(std::move(full_caps));
  node::NodeState full_state;
  full_state.set_in_flight(1);
  SetPoolInUse(&full_state, "gpu.h100", 1);
  full->UpdateState(std::move(full_state));

  node::NodeCapabilities free_caps;
  AddProtocol(&free_caps, "push");
  AddPool(&free_caps, "gpu.h100", 2);
  free->StoreCapabilities(std::move(free_caps));

  CapabilityAwareScheduler scheduler;
  auto offer = MakeOffer("inference", {{"gpu.h100", 1}});

  // "full" has zero shared-free gpu.h100 and is excluded.
  EXPECT_EQ(scheduler.Choose(*directory, offer), free);
}

TEST_F(CapabilityAwareSchedulerTest, ExcludesNodeAtConcurrencyLimit) {
  auto directory = MakeConnectedDirectory({"saturated", "free"});
  auto* saturated = directory->GetNode("saturated");
  auto* free = directory->GetNode("free");

  node::NodeCapabilities caps;
  node::NodeCapabilities caps_copy;
  AddProtocol(&caps, "push");
  AddPool(&caps, "cpu", 16);
  AddHandler(&caps, "echo", 2);
  caps_copy.CopyFrom(caps);
  saturated->StoreCapabilities(std::move(caps));
  free->StoreCapabilities(std::move(caps_copy));

  node::NodeState saturated_state;
  saturated_state.set_in_flight(2);
  SetTypeInFlight(&saturated_state, "echo", 2);
  saturated->UpdateState(std::move(saturated_state));

  CapabilityAwareScheduler scheduler;
  auto offer = MakeOffer("echo", {});

  EXPECT_EQ(scheduler.Choose(*directory, offer), free);
}

TEST_F(CapabilityAwareSchedulerTest, ChoosesLeastLoadedNodeByConcurrencyRatio) {
  auto directory = MakeConnectedDirectory({"loaded", "light"});
  auto* loaded = directory->GetNode("loaded");
  auto* light = directory->GetNode("light");

  node::NodeCapabilities caps;
  node::NodeCapabilities caps_copy;
  AddProtocol(&caps, "push");
  AddHandler(&caps, "echo", 100);
  caps_copy.CopyFrom(caps);
  loaded->StoreCapabilities(std::move(caps));
  light->StoreCapabilities(std::move(caps_copy));

  node::NodeState loaded_state;
  loaded_state.set_in_flight(5);
  SetTypeInFlight(&loaded_state, "echo", 5);
  loaded->UpdateState(std::move(loaded_state));

  node::NodeState light_state;
  light_state.set_in_flight(2);
  SetTypeInFlight(&light_state, "echo", 2);
  light->UpdateState(std::move(light_state));

  CapabilityAwareScheduler scheduler;
  auto offer = MakeOffer("echo", {});

  EXPECT_EQ(scheduler.Choose(*directory, offer), light);
}

TEST_F(CapabilityAwareSchedulerTest, TieBreaksByNodeWideInFlightCount) {
  auto directory = MakeConnectedDirectory({"busy", "idle"});
  auto* busy = directory->GetNode("busy");
  auto* idle = directory->GetNode("idle");

  // No per-type concurrency declared: both nodes have an equal (neutral) load
  // ratio, so the node-wide in-flight count decides.
  node::NodeCapabilities caps;
  node::NodeCapabilities caps_copy;
  AddProtocol(&caps, "push");
  caps_copy.CopyFrom(caps);
  busy->StoreCapabilities(std::move(caps));
  idle->StoreCapabilities(std::move(caps_copy));

  node::NodeState busy_state;
  busy_state.set_in_flight(5);
  busy->UpdateState(std::move(busy_state));

  node::NodeState idle_state;
  idle_state.set_in_flight(2);
  idle->UpdateState(std::move(idle_state));

  CapabilityAwareScheduler scheduler;
  auto offer = MakeOffer("echo", {});

  EXPECT_EQ(scheduler.Choose(*directory, offer), idle);
}

TEST_F(CapabilityAwareSchedulerTest, ExcludesNodesNotAdvertisingRequiredProtocol) {
  auto directory = MakeConnectedDirectory({"probe_only", "push_node"});
  auto* probe_only = directory->GetNode("probe_only");
  auto* push_node = directory->GetNode("push_node");

  node::NodeCapabilities probe_caps;
  AddProtocol(&probe_caps, "probe");
  AddPool(&probe_caps, "cpu", 8);
  probe_only->StoreCapabilities(std::move(probe_caps));

  node::NodeCapabilities push_caps;
  AddProtocol(&push_caps, "push");
  AddPool(&push_caps, "cpu", 8);
  push_node->StoreCapabilities(std::move(push_caps));

  CapabilityAwareScheduler scheduler;
  auto offer = MakeOffer("echo", {{"cpu", 1}});

  EXPECT_EQ(scheduler.Choose(*directory, offer), push_node);
}

TEST_F(CapabilityAwareSchedulerTest, ExcludesNodeWithoutMatchingHandler) {
  auto directory = MakeConnectedDirectory({"video_only", "generic"});
  auto* video_only = directory->GetNode("video_only");
  auto* generic = directory->GetNode("generic");

  node::NodeCapabilities video_caps;
  AddProtocol(&video_caps, "push");
  AddHandler(&video_caps, "video-encode", 4);
  video_only->StoreCapabilities(std::move(video_caps));

  node::NodeCapabilities generic_caps;
  AddProtocol(&generic_caps, "push");
  generic->StoreCapabilities(std::move(generic_caps));

  CapabilityAwareScheduler scheduler;
  auto offer = MakeOffer("echo", {});

  // "video_only" declares handlers but none for "echo".
  EXPECT_EQ(scheduler.Choose(*directory, offer), generic);
}

TEST_F(CapabilityAwareSchedulerTest, ReturnsNullWhenNoEligibleNode) {
  auto directory = MakeConnectedDirectory({"no_caps"});
  // A connected node that has not advertised yet is excluded until it does.
  CapabilityAwareScheduler scheduler;
  auto offer = MakeOffer("echo", {{"cpu", 1}});

  EXPECT_EQ(scheduler.Choose(*directory, offer), nullptr);
}

TEST_F(CapabilityAwareSchedulerTest, FactoryIsRegisteredAndCreatesScheduler) {
  auto* factory =
      extensions::Registry<extensions::SchedulerFactory>::instance().GetFactory("capability_aware");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->Name(), "capability_aware");

  extensions::FactoryContextImpl context(dispatcher_);
  auto config = factory->CreateEmptyConfigProto();
  auto scheduler = factory->Create(*config, context);
  ASSERT_NE(scheduler, nullptr);
  EXPECT_EQ(scheduler->RequiredProtocol(), "push");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::extensions::schedulers
