#include "nodeagent/core/local_receiver_registry.hh"

#include <utility>

namespace strij::nodeagent {

void LocalReceiverRegistry::Put(std::string task_id, gateway::ResultReceiverPtr receiver) {
  receivers_.insert_or_assign(std::move(task_id), std::move(receiver));
}

auto LocalReceiverRegistry::Get(const std::string& task_id) -> gateway::ResultReceiver* {
  auto iter = receivers_.find(task_id);
  return iter != receivers_.end() ? iter->second.get() : nullptr;
}

auto LocalReceiverRegistry::Erase(const std::string& task_id) -> bool {
  return receivers_.erase(task_id) > 0;
}

auto LocalReceiverRegistry::Empty() const -> bool { return receivers_.empty(); }

auto LocalReceiverRegistry::Size() const -> size_t { return receivers_.size(); }

} // namespace strij::nodeagent
