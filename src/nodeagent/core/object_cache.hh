#pragma once

#include <string>
#include <unordered_map>

#include "common/task/task.pb.h"
#include "strij/nodeagent/object_cache.hh"

namespace strij::nodeagent {

// Trivial in-memory object cache: stores fetched data objects keyed by their
// (source, key) pair in a hash map. Consumed through the abstract
// strij::nodeagent::ObjectCache contract; the nodeagent process owns one
// instance shared by all schedulers and fetchers.
class InMemoryObjectCache final : public ObjectCache {
public:
  InMemoryObjectCache() = default;

  // ObjectCache
  void Populate(const task::DataRef& ref, std::string data) override;
  [[nodiscard]] auto IsCached(const task::DataRef& ref) const -> bool override;

private:
  std::unordered_map<std::string, std::string> objects_;
};

} // namespace strij::nodeagent