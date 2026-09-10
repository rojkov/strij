#include "nodeagent/core/object_cache.hh"

#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "common/task/task.pb.h"

namespace strij::nodeagent {

namespace {

constexpr std::string_view kSourceKeySeparator = ":";

auto CacheKey(const task::DataRef& ref) -> std::string {
  return absl::StrCat(ref.source(), kSourceKeySeparator, ref.key());
}

} // namespace

void InMemoryObjectCache::Populate(const task::DataRef& ref, std::string data) {
  objects_.insert_or_assign(CacheKey(ref), std::move(data));
}

auto InMemoryObjectCache::IsCached(const task::DataRef& ref) const -> bool {
  return objects_.contains(CacheKey(ref));
}

} // namespace strij::nodeagent