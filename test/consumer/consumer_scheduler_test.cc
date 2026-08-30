#include <memory>
#include <string>

#include "consumer_node_scheduler.hh"
#include "consumer_scheduler.hh"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "strij/extensions/extension_registry.hh"

namespace strij_consumer {
namespace {

using ::testing::NotNull;

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(ConsumerSchedulerTest, FactoryIsRegisteredByName) {
  strij::extensions::Registry<strij::gateway::GatewaySchedulerFactory>& registry =
      strij::extensions::Registry<strij::gateway::GatewaySchedulerFactory>::instance();

  strij::gateway::GatewaySchedulerFactory* factory =
      registry.GetFactory(std::string(kConsumerSchedulerName));

  ASSERT_THAT(factory, NotNull());
  EXPECT_EQ(factory->Name(), kConsumerSchedulerName);
}

TEST(ConsumerSchedulerTest, FactoryProducesConfigProtoSkeletons) {
  strij::extensions::Registry<strij::gateway::GatewaySchedulerFactory>& registry =
      strij::extensions::Registry<strij::gateway::GatewaySchedulerFactory>::instance();
  strij::gateway::GatewaySchedulerFactory* factory =
      registry.GetFactory(std::string(kConsumerSchedulerName));
  ASSERT_THAT(factory, NotNull());

  EXPECT_NE(factory->CreateEmptyConfigProto(), nullptr);
}

TEST(ConsumerSchedulerTest, SchedulerAnnouncesItsProtocol) {
  std::unique_ptr<ConsumerScheduler> scheduler = std::make_unique<ConsumerScheduler>();
  EXPECT_EQ(scheduler->RequiredProtocol(), "push");
}

TEST(ConsumerNodeSchedulerTest, FactoryIsRegisteredByName) {
  strij::extensions::Registry<strij::nodeagent::NodeSchedulerFactory>& registry =
      strij::extensions::Registry<strij::nodeagent::NodeSchedulerFactory>::instance();

  strij::nodeagent::NodeSchedulerFactory* factory =
      registry.GetFactory(std::string(kConsumerNodeSchedulerName));

  ASSERT_THAT(factory, NotNull());
  EXPECT_EQ(factory->Name(), kConsumerNodeSchedulerName);
  EXPECT_NE(factory->CreateEmptyConfigProto(), nullptr);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij_consumer