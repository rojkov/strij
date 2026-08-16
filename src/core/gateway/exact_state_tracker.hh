#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "core/node/capabilities.pb.h"

namespace strij::gateway {

// Tracks the gateway's view of per-node load by exact accounting of task
// submissions (increment) and final results / rejections (decrement), keyed by
// task id so the per-task resolved requirements can be unwound on completion.
// kNodeState snapshots from the nodeagent are applied as a verification /
// correction signal, replacing the exact counts with the reported snapshot.
class ExactStateTracker final {
public:
  // Records a task sent to `node_id` and reserves the pools in `requirements`.
  void RecordSubmission(std::string task_id, std::string node_id,
                        const node::ResourceRequirements& requirements);
  // Unwinds the task recorded by RecordSubmission (final result or rejection).
  // A no-op for an unknown task id.
  void RecordCompletion(const std::string& task_id);
  // Replaces the node's exact counts with the reported snapshot.
  void ApplyStateSnapshot(const node::NodeState& state);

  auto InFlight(const std::string& node_id) const -> uint64_t;
  auto PoolInUse(const std::string& node_id, std::string_view pool) const -> uint64_t;

private:
  struct TaskRecord {
    std::string node_id_;
    node::ResourceRequirements requirements_;
  };

  class NodeAccounting {
  public:
    void Decrement(const node::ResourceRequirements& requirements);
    void Increment(const node::ResourceRequirements& requirements);
    void ApplyStateSnapshot(const node::NodeState& state);

    auto InFlight() const -> uint64_t;
    auto PoolInUse(std::string_view pool) const -> uint64_t;

  private:
    uint64_t in_flight_{0};
    std::unordered_map<std::string, uint64_t> pools_;
  };

  auto accounting(const std::string& node_id) -> NodeAccounting&;

  std::unordered_map<std::string, NodeAccounting> per_node_;
  std::unordered_map<std::string, TaskRecord> tasks_;
};

} // namespace strij::gateway
