#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

#include "strij/gateway/result_receiver_storage.hh"

namespace strij::gateway {

class ExactStateTracker;

// Concrete per-task result receiver registry backing the gateway. Consumed
// through the abstract strij::gateway::ResultReceiverStorage contract;
// extension authors SHALL NOT subclass this Impl.
class ResultReceiverStorageImpl final : public ResultReceiverStorage {
public:
  explicit ResultReceiverStorageImpl(ExactStateTracker* state_tracker = nullptr)
      : state_tracker_{state_tracker} {}

  void Put(std::string task_id, ResultReceiverPtr receiver, std::string node_id) override {
    receivers_.emplace(task_id, std::move(receiver));
    node_of_task_.emplace(std::move(task_id), std::move(node_id));
  }

  auto Get(const std::string& task_id) -> ResultReceiver* override {
    auto iter = receivers_.find(task_id);
    return iter != receivers_.end() ? iter->second.get() : nullptr;
  }

  void Erase(const std::string& task_id) override {
    receivers_.erase(task_id);
    node_of_task_.erase(task_id);
  }

  auto Empty() const -> bool override { return receivers_.empty(); }
  auto Size() const -> size_t override { return receivers_.size(); }

  // Cleans up all receivers for tasks routed to `node_id`. Delivers errors to
  // still-connected HTTP clients, removes receivers, and records completions
  // in the state tracker.
  void NotifyNodeDisconnected(const std::string& node_id) override;

  // Cleans up the receiver for a task whose HTTP client disconnected before
  // the task completed. Removes the receiver and records completion in the
  // state tracker so the node's in-flight accounting is unwound.
  void NotifyClientDisconnected(const std::string& task_id) override;

private:
  ExactStateTracker* state_tracker_;
  std::unordered_map<std::string, ResultReceiverPtr> receivers_;
  std::unordered_map<std::string, std::string> node_of_task_;
};

} // namespace strij::gateway
