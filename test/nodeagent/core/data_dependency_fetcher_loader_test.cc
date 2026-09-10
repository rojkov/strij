#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "test/mocks/extensions/extensions_mocks.hh"

#include "common/config/extensions.pb.h"
#include "nodeagent/core/data_dependency_fetcher_router.hh"
#include "nodeagent/core/object_cache.hh"
#include "strij/extensions/extension_registry.hh"
#include "google/protobuf/empty.pb.h"
#include "gtest/gtest.h"

namespace strij::nodeagent {
namespace {

using ::testing::Return;

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST(DataDependencyFetcherLoaderTest, EmptyListBuildsEmptyVector) {
  extensions::MockNodeagentFactoryContext context;
  ::google::protobuf::RepeatedPtrField<config::ExtensionConfig> configs;

  auto result = BuildDataDependencyFetchers(configs, context);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().empty());
}

TEST(DataDependencyFetcherLoaderTest, UnknownFetcherNameReturnsError) {
  extensions::MockNodeagentFactoryContext context;
  ::google::protobuf::RepeatedPtrField<config::ExtensionConfig> configs;
  auto* ext = configs.Add();
  ext->set_name("no_such_fetcher");

  auto result = BuildDataDependencyFetchers(configs, context);

  ASSERT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("no_such_fetcher"), std::string::npos);
}

TEST(DataDependencyFetcherLoaderTest, BuildInstantiatesFetcherFromConfig) {
  auto factory = std::make_unique<extensions::MockDataDependencyFetcherFactory>();
  EXPECT_CALL(*factory, Name()).WillRepeatedly(Return("mock_fetcher"));
  EXPECT_CALL(*factory, CreateEmptyConfigProto())
      .WillOnce(Return(std::make_unique<google::protobuf::Empty>()));
  auto fetcher = std::make_unique<extensions::MockDataDependencyFetcher>();
  EXPECT_CALL(*factory, Create(::testing::_, ::testing::_))
      .WillOnce([&fetcher](const ::google::protobuf::Message& /*config*/,
                           extensions::NodeagentFactoryContext& /*context*/) {
        return std::move(fetcher);
      });
  // The singleton registry owns the factory for the program lifetime.
  ::testing::Mock::AllowLeak(factory.get());
  extensions::Registry<nodeagent::DataDependencyFetcherFactory>::instance().RegisterFactory(
      "mock_fetcher", factory.release());

  extensions::MockNodeagentFactoryContext context;
  ::google::protobuf::RepeatedPtrField<config::ExtensionConfig> configs;
  auto* ext = configs.Add();
  ext->set_name("mock_fetcher");
  google::protobuf::Empty typed;
  ext->mutable_typed_config()->PackFrom(typed);

  auto result = BuildDataDependencyFetchers(configs, context);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().size(), 1U);
}

TEST(DataDependencyFetcherLoaderTest, DuplicateSchemeAcrossFetchersFailsRouterBuild) {
  static constexpr std::string_view kRayScheme[] = {"ray"};

  auto register_factory = [](const char* name) {
    auto factory = std::make_unique<extensions::MockDataDependencyFetcherFactory>();
    EXPECT_CALL(*factory, Name()).WillRepeatedly(Return(name));
    EXPECT_CALL(*factory, CreateEmptyConfigProto())
        .WillRepeatedly(
            []() { return std::make_unique<google::protobuf::Empty>(); });
    EXPECT_CALL(*factory, Create(::testing::_, ::testing::_))
        .WillRepeatedly([](const ::google::protobuf::Message&,
                           extensions::NodeagentFactoryContext&) {
          auto fetcher = std::make_unique<extensions::MockDataDependencyFetcher>();
          EXPECT_CALL(*fetcher, HandledSourceTypes())
              .WillRepeatedly(Return(std::span<const std::string_view>(kRayScheme)));
          EXPECT_CALL(*fetcher, Fetch(::testing::_, ::testing::_, ::testing::_, ::testing::_))
              .Times(0);
          return fetcher;
        });
    ::testing::Mock::AllowLeak(factory.get());
    extensions::Registry<nodeagent::DataDependencyFetcherFactory>::instance().RegisterFactory(
        name, factory.release());
  };
  register_factory("dup_fetcher_a");
  register_factory("dup_fetcher_b");

  extensions::MockNodeagentFactoryContext context;
  ::google::protobuf::RepeatedPtrField<config::ExtensionConfig> configs;
  configs.Add()->set_name("dup_fetcher_a");
  configs.Add()->set_name("dup_fetcher_b");

  auto fetchers_result = BuildDataDependencyFetchers(configs, context);
  ASSERT_TRUE(fetchers_result.ok());

  InMemoryObjectCache cache;
  auto router_result =
      DataDependencyFetcherRouter::Build(cache, std::move(fetchers_result).value());
  ASSERT_FALSE(router_result.ok());
  EXPECT_NE(router_result.status().message().find("ray"), std::string::npos);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent