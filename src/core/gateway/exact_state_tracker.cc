#include "core/gateway/exact_state_tracker.hh"

#include <string>
#include <utility>

#include "core/node/capabilities.pb.h"

namespace strij::gateway {

auto ExactStateTracker::accounting(const std::string& node_id) -> NodeAccounting& {
  return per_node_[node_id];
}

void ExactStateTracker::RecordSubmission(std::string task_id, std::string node_id,
                                         const strij::node::ResourceRequirements& requirements) {
  const std::string task_key = task_id;
  tasks_.insert_or_assign(std::move(task_id), TaskRecord{std::move(node_id), requirements});

  auto& account = accounting(tasks_.at(task_key).node_id);
  ++account.in_flight;
  for (const auto& [pool, amount] : requirements.resources()) {
    account.pools[pool] += amount;
  }
}

void ExactStateTracker::RecordCompletion(const std::string& task_id) {
  const auto task_iter = tasks_.find(task_id);
  if (task_iter == tasks_.end()) {
    return;
  }

  auto& account = accounting(task_iter->second.node_id);
  if (account.in_flight > 0) {
    --account.in_flight;
  }
  decrement(account, task_iter->second.requirements);
  tasks_.erase(task_iter);
}

void ExactStateTracker::ApplyStateSnapshot(const strij::node::NodeState& state) {
  auto& account = accounting(state.node_id());
  account.in_flight = state.in_flight();
  account.pools.clear();
  for (const auto& usage : state.pools()) {
    account.pools[usage.pool()] = usage.in_use();
  }
}

auto ExactStateTracker::InFlight(const std::string& node_id) const -> uint64_t {
  const auto iter = per_node_.find(node_id);
  return iter == per_node_.end() ? 0 : iter->second.in_flight;
}

auto ExactStateTracker::PoolInUse(const std::string& node_id, std::string_view pool) const
    -> uint64_t {
  const auto node_iter = per_node_.find(node_id);
  if (node_iter == per_node_.end()) {
    return 0;
  }
  const auto pool_iter = node_iter->second.pools.find(std::string(pool));
  return pool_iter == node_iter->second.pools.end() ? 0 : pool_iter->second;
}

void ExactStateTracker::decrement(NodeAccounting& account,
                                  const strij::node::ResourceRequirements& requirements) {
  for (const auto& [pool, amount] : requirements.resources()) {
    auto iter = account.pools.find(pool);
    if (iter == account.pools.end()) {
      continue;
    }
    if (amount >= iter->second) {
      account.pools.erase(iter);
    } else {
      iter->second -= amount;
    }
  }
}

} // namespace strij::gateway
