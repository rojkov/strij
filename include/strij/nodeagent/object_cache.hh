#pragma once

#include <memory>
#include <string>

#include "common/task/task.pb.h"
#include "strij/common/pure.hh"

namespace strij::nodeagent {

// Pure-abstract contract for the nodeagent's node-global object cache: a
// task-agnostic store of fetched data objects keyed by their (source, key)
// pair. Fetchers populate it when a data dependency finishes downloading;
// node schedulers query it to decide whether a probe's dependencies are ready.
// One instance is owned by the nodeagent process and shared by all schedulers
// and fetchers via DataDependencyFetcherDeps::object_cache_. Extension authors
// consume it through this interface rather than a concrete implementation.
class ObjectCache {
public:
  ObjectCache() = default;
  virtual ~ObjectCache() = default;

  ObjectCache(const ObjectCache&) = delete;
  auto operator=(const ObjectCache&) -> ObjectCache& = delete;
  ObjectCache(ObjectCache&&) noexcept = delete;
  auto operator=(ObjectCache&&) noexcept -> ObjectCache& = delete;

  // Stores `data` for `ref`, keyed by its (source, key) pair. Overwrites any
  // previously cached object with the same key.
  virtual void Populate(const task::DataRef& ref, std::string data) PURE;

  // Returns true iff an object for `ref` was previously Populate()d.
  [[nodiscard]] virtual auto IsCached(const task::DataRef& ref) const -> bool PURE;
};

using ObjectCachePtr = std::unique_ptr<ObjectCache>;
using ObjectCacheSharedPtr = std::shared_ptr<ObjectCache>;

} // namespace strij::nodeagent