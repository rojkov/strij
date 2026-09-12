#include "common/task/task.pb.h"
#include "nodeagent/core/object_cache.hh"
#include "gtest/gtest.h"

namespace strij::nodeagent {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

auto MakeRef(const std::string& source, const std::string& key) {
  task::DataRef ref;
  ref.set_source(source);
  ref.set_key(key);
  return ref;
}

TEST(InMemoryObjectCacheTest, PopulatedRefIsCached) {
  InMemoryObjectCache cache;

  auto ref = MakeRef("http", "blob/1");
  cache.Populate(ref, "payload");

  EXPECT_TRUE(cache.IsCached(ref));
}

TEST(InMemoryObjectCacheTest, UnpopulatedRefIsNotCached) {
  InMemoryObjectCache cache;

  EXPECT_FALSE(cache.IsCached(MakeRef("http", "blob/1")));
}

TEST(InMemoryObjectCacheTest, KeysAreSourceAndKeyScoped) {
  InMemoryObjectCache cache;

  cache.Populate(MakeRef("http", "blob/1"), "payload");

  // Same key, different source is a distinct object.
  EXPECT_FALSE(cache.IsCached(MakeRef("s3", "blob/1")));
}

TEST(InMemoryObjectCacheTest, PopulateOverwrites) {
  InMemoryObjectCache cache;

  cache.Populate(MakeRef("http", "blob/1"), "first");
  cache.Populate(MakeRef("http", "blob/1"), "second");

  EXPECT_TRUE(cache.IsCached(MakeRef("http", "blob/1")));
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent