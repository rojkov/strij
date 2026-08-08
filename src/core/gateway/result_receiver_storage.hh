#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>

#include "strij/common/pure.hh"

namespace strij::gateway {

class ResultReceiver {
public:
  virtual ~ResultReceiver() = default;

  // Delivers one result chunk of a task. `is_final` marks the last result.
  virtual void Deliver(std::span<const std::byte> value, bool is_final) PURE;
};

using ResultReceiverPtr = std::unique_ptr<ResultReceiver>;

class ResultReceiverStorage {
public:
  void put(std::string task_id, ResultReceiverPtr receiver) {
    receivers_.emplace(std::move(task_id), std::move(receiver));
  }

  auto get(const std::string& task_id) -> ResultReceiver* {
    auto it = receivers_.find(task_id);
    return it != receivers_.end() ? it->second.get() : nullptr;
  }

  void erase(const std::string& task_id) { receivers_.erase(task_id); }

  auto empty() const -> bool { return receivers_.empty(); }
  auto size() const -> size_t { return receivers_.size(); }

private:
  // TODO: how to clean up the storage when connections get dropped? Don't forget about asynchronous
  // tasks.
  std::unordered_map<std::string, ResultReceiverPtr> receivers_;
};

} // namespace strij::gateway
