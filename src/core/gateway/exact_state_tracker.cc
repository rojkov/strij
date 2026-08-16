#include "core/gateway/exact_state_tracker.hh"

#include <string>
#include <utility>

#include "core/node/capabilities.pb.h"

namespace strij::gateway {

auto ExactStateTracker::accounting(const std::string& node_id) -> NodeAccounting& {
  // Get or create.
  return per_node_[node_id];
}

void ExactStateTracker::RecordSubmission(std::string task_id, std::string node_id,
                                         const node::ResourceRequirements& requirements) {
  auto& account = accounting(node_id);
  account.Increment(requirements);

  tasks_.insert_or_assign(std::move(task_id), TaskRecord{.node_id_ = std::move(node_id),
                                                         .requirements_ = requirements});
}

void ExactStateTracker::RecordCompletion(const std::string& task_id) {
  const auto task_iter = tasks_.find(task_id);
  if (task_iter == tasks_.end()) {
    return;
  }

  auto& account = accounting(task_iter->second.node_id_);

  account.Decrement(task_iter->second.requirements_);
  tasks_.erase(task_iter);
}

void ExactStateTracker::ApplyStateSnapshot(const node::NodeState& state) {
  auto& account = accounting(state.node_id());
  account.ApplyStateSnapshot(state);
}

auto ExactStateTracker::InFlight(const std::string& node_id) const -> uint64_t {
  const auto iter = per_node_.find(node_id);
  return iter == per_node_.end() ? 0 : iter->second.InFlight();
}

auto ExactStateTracker::PoolInUse(const std::string& node_id, std::string_view pool) const
    -> uint64_t {
  const auto node_iter = per_node_.find(node_id);
  if (node_iter == per_node_.end()) {
    return 0;
  }

  return node_iter->second.PoolInUse(pool);
}

void ExactStateTracker::NodeAccounting::ApplyStateSnapshot(const node::NodeState& state) {
  in_flight_ = state.in_flight();
  pools_.clear();

  for (const auto& usage : state.pools()) {
    pools_[usage.pool()] = usage.in_use();
  }
}

void ExactStateTracker::NodeAccounting::Decrement(const node::ResourceRequirements& requirements) {
  if (in_flight_ > 0) {
    --in_flight_;
  }

  for (const auto& [pool, amount] : requirements.resources()) {
    auto iter = pools_.find(pool);
    if (iter == pools_.end()) {
      continue;
    }

    if (amount >= iter->second) {
      pools_.erase(iter);
    } else {
      iter->second -= amount;
    }
  }
}

void ExactStateTracker::NodeAccounting::Increment(const node::ResourceRequirements& requirements) {
  ++in_flight_;

  for (const auto& [pool, amount] : requirements.resources()) {
    pools_[pool] += amount;
  }
}

auto ExactStateTracker::NodeAccounting::PoolInUse(std::string_view pool) const -> uint64_t {
  const auto pool_iter = pools_.find(std::string(pool));
  return pool_iter == pools_.end() ? 0 : pool_iter->second;
}

auto ExactStateTracker::NodeAccounting::InFlight() const -> uint64_t { return in_flight_; }

} // namespace strij::gateway
