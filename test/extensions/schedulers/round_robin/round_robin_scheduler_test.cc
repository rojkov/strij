#include <memory>
#include <string>
#include <utility>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "core/extensions/extension_registry.hh"
#include "core/gateway/node_directory.hh"
#include "core/io/protocol_parser.hh"
#include "core/node/capabilities.pb.h"
#include "extensions/schedulers/round_robin/round_robin_scheduler.hh"
#include "extensions/schedulers/scheduler.hh"
#include "gtest/gtest.h"

namespace strij::extensions::schedulers {
namespace {

class RoundRobinSchedulerTest : public ::testing::Test {
protected:
  std::shared_ptr<strij::event::MockDispatcher> dispatcher_{
      std::make_shared<strij::event::MockDispatcher>()};

  // Creates a directory and brings every node into the connected state.
  auto MakeConnectedDirectory(std::initializer_list<std::string> ids)
      -> std::unique_ptr<strij::gateway::NodeDirectory> {
    auto directory = std::make_unique<strij::gateway::NodeDirectory>(
        dispatcher_, [](strij::io::Connection&) -> std::unique_ptr<strij::io::ProtocolParser> {
          return std::make_unique<strij::io::TrivialParser>();
        });
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
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(RoundRobinSchedulerTest, RotatesOverAvailableNodes) {
  auto directory = MakeConnectedDirectory({"A", "B"});
  RoundRobinScheduler scheduler;
  TaskOffer offer{};

  EXPECT_EQ(scheduler.Choose(*directory, offer), directory->GetNode("A"));
  EXPECT_EQ(scheduler.Choose(*directory, offer), directory->GetNode("B"));
  EXPECT_EQ(scheduler.Choose(*directory, offer), directory->GetNode("A"));
  EXPECT_EQ(scheduler.Choose(*directory, offer), directory->GetNode("B"));
}

TEST_F(RoundRobinSchedulerTest, ReturnsNullWhenNoNodesAvailable) {
  auto directory = MakeConnectedDirectory({});
  RoundRobinScheduler scheduler;
  TaskOffer offer{};

  EXPECT_EQ(scheduler.Choose(*directory, offer), nullptr);
}

TEST_F(RoundRobinSchedulerTest, ExcludesNodesNotAdvertisingRequiredProtocol) {
  auto directory = MakeConnectedDirectory({"push", "probe", "quiet"});
  auto* push_node = directory->GetNode("push");
  auto* probe_node = directory->GetNode("probe");

  strij::node::NodeCapabilities push_caps;
  push_caps.add_scheduling_protocols()->set_name("push");
  push_node->StoreCapabilities(std::move(push_caps));

  strij::node::NodeCapabilities probe_caps;
  probe_caps.add_scheduling_protocols()->set_name("probe");
  probe_node->StoreCapabilities(std::move(probe_caps));
  // "quiet" stays connected without an advertisement: it is inside the
  // handshake window and remains eligible.

  RoundRobinScheduler scheduler;
  TaskOffer offer{};

  auto* first = scheduler.Choose(*directory, offer);
  auto* second = scheduler.Choose(*directory, offer);
  EXPECT_NE(first, nullptr);
  EXPECT_NE(first, probe_node);
  EXPECT_NE(second, nullptr);
  EXPECT_NE(second, probe_node);
}

TEST_F(RoundRobinSchedulerTest, FactoryIsRegisteredAndCreatesScheduler) {
  auto* factory =
      strij::extensions::Registry<strij::extensions::SchedulerFactory>::instance().GetFactory(
          "round_robin");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->Name(), "round_robin");

  strij::extensions::FactoryContextImpl context(dispatcher_);
  auto config = factory->CreateEmptyConfigProto();
  auto scheduler = factory->Create(*config, context);
  ASSERT_NE(scheduler, nullptr);
  EXPECT_EQ(scheduler->RequiredProtocol(), "push");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::extensions::schedulers
