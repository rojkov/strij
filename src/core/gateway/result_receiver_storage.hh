#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "strij/common/pure.hh"

namespace strij::gateway {

class ExactStateTracker;

class ResultReceiver {
public:
  ResultReceiver() = default;
  virtual ~ResultReceiver() = default;

  ResultReceiver(const ResultReceiver&) = delete;
  auto operator=(const ResultReceiver&) -> ResultReceiver& = delete;
  ResultReceiver(ResultReceiver&&) noexcept = delete;
  auto operator=(ResultReceiver&&) noexcept -> ResultReceiver& = delete;

  // Delivers one result chunk of a task. `is_final` marks the last result.
  virtual void Deliver(std::span<const std::byte> value, bool is_final) PURE;
  // Delivers an error outcome (e.g. the node rejected the task); the client
  // connection must not hang. Implementations may finalize their framing.
  virtual void DeliverError(std::string_view reason) PURE;
};

using ResultReceiverPtr = std::unique_ptr<ResultReceiver>;

class ResultReceiverStorage {
public:
  explicit ResultReceiverStorage(ExactStateTracker* state_tracker = nullptr)
      : state_tracker_{state_tracker} {}

  void Put(std::string task_id, ResultReceiverPtr receiver, std::string node_id) {
    receivers_.emplace(task_id, std::move(receiver));
    node_of_task_.emplace(std::move(task_id), std::move(node_id));
  }

  auto Get(const std::string& task_id) -> ResultReceiver* {
    auto iter = receivers_.find(task_id);
    return iter != receivers_.end() ? iter->second.get() : nullptr;
  }

  void Erase(const std::string& task_id) {
    receivers_.erase(task_id);
    node_of_task_.erase(task_id);
  }

  auto Empty() const -> bool { return receivers_.empty(); }
  auto Size() const -> size_t { return receivers_.size(); }

  // Cleans up all receivers for tasks routed to `node_id`. Delivers errors to
  // still-connected HTTP clients, removes receivers, and records completions
  // in the state tracker.
  void NotifyNodeDisconnected(const std::string& node_id);

private:
  ExactStateTracker* state_tracker_;
  std::unordered_map<std::string, ResultReceiverPtr> receivers_;
  std::unordered_map<std::string, std::string> node_of_task_;
};

} // namespace strij::gateway
