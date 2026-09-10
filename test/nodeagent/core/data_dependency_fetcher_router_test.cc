#include "nodeagent/core/data_dependency_fetcher_router.hh"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "common/task/task.pb.h"
#include "nodeagent/core/object_cache.hh"
#include "test/mocks/event/mocks.hh"
#include "test/mocks/common/common_mocks.hh"
#include "gtest/gtest.h"

namespace strij::nodeagent {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

using task::DataRef;

auto MakeRef(std::string source, std::string key) {
  DataRef ref;
  ref.set_source(std::move(source));
  ref.set_key(std::move(key));
  return ref;
}

// Records Fetch invocations and the stable task-id reference it received, so a
// test can assert both routing and the DEP_COMPLETED args_ stability contract
// (the fetch must be handed a reference the router keeps alive).
class RecordingFetcher final : public extensions::DataDependencyFetcher {
public:
  struct FetchCall {
    std::string source;
    std::string key;
    std::string task_id;
  };

  explicit RecordingFetcher(std::initializer_list<std::string_view> schemes)
      : schemes_{schemes.begin(), schemes.end()} {}

  void Fetch(const task::DataRef& ref, const std::string& task_id,
             event::Dispatcher& /*dispatcher*/, event::CommandHandler* /*destination*/) override {
    calls_.push_back(FetchCall{.source = ref.source(), .key = ref.key(), .task_id = task_id});
  }

  auto HandledSourceTypes() const -> std::span<const std::string_view> override {
    return schemes_;
  }

  std::vector<std::string_view> schemes_;
  std::vector<FetchCall> calls_;
};

TEST(DataDependencyFetcherRouterTest, BuildFailsOnDuplicateScheme) {
  InMemoryObjectCache cache;
  std::vector<extensions::DataDependencyFetcherPtr> fetchers;
  fetchers.push_back(std::make_unique<RecordingFetcher>(
      std::initializer_list<std::string_view>{"ray", "http"}));
  fetchers.push_back(std::make_unique<RecordingFetcher>(
      std::initializer_list<std::string_view>{"http"}));

  auto result = DataDependencyFetcherRouter::Build(cache, std::move(fetchers));

  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.status().message().find("http"), std::string::npos);
}

TEST(DataDependencyFetcherRouterTest, RoutesEachRefToOwningFetcher) {
  InMemoryObjectCache cache;
  std::vector<extensions::DataDependencyFetcherPtr> fetchers;
  auto* ray = new RecordingFetcher(std::initializer_list<std::string_view>{"ray"});
  auto* http = new RecordingFetcher(std::initializer_list<std::string_view>{"http"});
  fetchers.push_back(extensions::DataDependencyFetcherPtr(ray));
  fetchers.push_back(extensions::DataDependencyFetcherPtr(http));
  auto router = DataDependencyFetcherRouter::Build(cache, std::move(fetchers));

  ASSERT_TRUE(router.ok());

  google::protobuf::RepeatedPtrField<DataRef> deps;
  *deps.Add() = MakeRef("ray", "obj_abc");
  *deps.Add() = MakeRef("http", "blob/1");
  *deps.Add() = MakeRef("ray", "obj_def");
  event::MockDispatcher dispatcher;
  event::DummyOwner destination;
  (*router)->FetchAll(deps, "task-1", dispatcher, &destination);

  ASSERT_EQ(ray->calls_.size(), 2U);
  EXPECT_EQ(ray->calls_[0].key, "obj_abc");
  EXPECT_EQ(ray->calls_[1].key, "obj_def");
  ASSERT_EQ(http->calls_.size(), 1U);
  EXPECT_EQ(http->calls_[0].key, "blob/1");
}

TEST(DataDependencyFetcherRouterTest, UnknownSourceIsSkippedAndNeverGates) {
  InMemoryObjectCache cache;
  std::vector<extensions::DataDependencyFetcherPtr> fetchers;
  auto* ray = new RecordingFetcher(std::initializer_list<std::string_view>{"ray"});
  fetchers.push_back(extensions::DataDependencyFetcherPtr(ray));
  auto router = DataDependencyFetcherRouter::Build(cache, std::move(fetchers));

  ASSERT_TRUE(router.ok());

  // Unfetchable scheme: FetchAll skips it (no fetch started for "nfs").
  google::protobuf::RepeatedPtrField<DataRef> deps;
  *deps.Add() = MakeRef("nfs", "export1");
  event::MockDispatcher dispatcher;
  event::DummyOwner destination;
  (*router)->FetchAll(deps, "task-1", dispatcher, &destination);
  EXPECT_TRUE(ray->calls_.empty());

  // And it never blocks readiness.
  EXPECT_TRUE((*router)->AllCached(deps));
}

TEST(DataDependencyFetcherRouterTest, FetchAllEmptyDepsNoOps) {
  InMemoryObjectCache cache;
  std::vector<extensions::DataDependencyFetcherPtr> fetchers;
  auto* ray = new RecordingFetcher(std::initializer_list<std::string_view>{"ray"});
  fetchers.push_back(extensions::DataDependencyFetcherPtr(ray));
  auto router = DataDependencyFetcherRouter::Build(cache, std::move(fetchers));

  ASSERT_TRUE(router.ok());

  google::protobuf::RepeatedPtrField<DataRef> deps;
  event::MockDispatcher dispatcher;
  event::DummyOwner destination;
  (*router)->FetchAll(deps, "task-1", dispatcher, &destination);

  EXPECT_TRUE(ray->calls_.empty());
  EXPECT_TRUE((*router)->AllCached(deps));
}

TEST(DataDependencyFetcherRouterTest, AllCachedReflectsObjectCache) {
  InMemoryObjectCache cache;
  google::protobuf::RepeatedPtrField<DataRef> cached_dep;
  *cached_dep.Add() = MakeRef("ray", "obj_abc");
  cache.Populate(cached_dep.Get(0), "payload");

  google::protobuf::RepeatedPtrField<DataRef> mixed_deps;
  *mixed_deps.Add() = cached_dep.Get(0);
  *mixed_deps.Add() = MakeRef("ray", "obj_not_cached");

  std::vector<extensions::DataDependencyFetcherPtr> fetchers;
  fetchers.push_back(std::make_unique<RecordingFetcher>(
      std::initializer_list<std::string_view>{"ray"}));
  auto router = DataDependencyFetcherRouter::Build(cache, std::move(fetchers));

  ASSERT_TRUE(router.ok());
  EXPECT_TRUE((*router)->AllCached(cached_dep));
  EXPECT_FALSE((*router)->AllCached(mixed_deps));
}

TEST(DataDependencyFetcherRouterTest, EmptyRouterIsTriviallyReadyAndNoOps) {
  InMemoryObjectCache cache;
  auto router = DataDependencyFetcherRouter::Build(
      cache, std::vector<extensions::DataDependencyFetcherPtr>{});

  ASSERT_TRUE(router.ok());
  EXPECT_TRUE((*router)->empty());

  google::protobuf::RepeatedPtrField<DataRef> deps;
  *deps.Add() = MakeRef("ray", "obj_abc");
  event::MockDispatcher dispatcher;
  event::DummyOwner destination;
  (*router)->FetchAll(deps, "task-1", dispatcher, &destination);
  EXPECT_TRUE((*router)->AllCached(deps));
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent