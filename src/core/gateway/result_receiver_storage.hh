#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

namespace carrot::gateway {

class ResultReceiver {
public:
  virtual ~ResultReceiver() = default;

  virtual void Deliver(std::span<const std::byte> value) = 0;
};

using ResultReceiverPtr = std::unique_ptr<ResultReceiver>;

class ResultReceiverStorage {
public:
  void put(uint64_t task_id, ResultReceiverPtr receiver) {
    receivers_.emplace(task_id, std::move(receiver));
  }

  auto get(uint64_t task_id) -> ResultReceiver* {
    auto it = receivers_.find(task_id);
    return it != receivers_.end() ? it->second.get() : nullptr;
  }

  void erase(uint64_t task_id) { receivers_.erase(task_id); }

  auto empty() const -> bool { return receivers_.empty(); }
  auto size() const -> size_t { return receivers_.size(); }

private:
  std::unordered_map<uint64_t, ResultReceiverPtr> receivers_;
};

} // namespace carrot::gateway
