#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "strij/common/pure.hh"

namespace strij::gateway {

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
  void Put(std::string task_id, ResultReceiverPtr receiver) {
    receivers_.emplace(std::move(task_id), std::move(receiver));
  }

  auto Get(const std::string& task_id) -> ResultReceiver* {
    auto iter = receivers_.find(task_id);
    return iter != receivers_.end() ? iter->second.get() : nullptr;
  }

  void Erase(const std::string& task_id) { receivers_.erase(task_id); }

  auto Empty() const -> bool { return receivers_.empty(); }
  auto Size() const -> size_t { return receivers_.size(); }

private:
  // TODO: how to clean up the storage when connections get dropped? Don't forget about asynchronous
  // tasks.
  std::unordered_map<std::string, ResultReceiverPtr> receivers_;
};

} // namespace strij::gateway
