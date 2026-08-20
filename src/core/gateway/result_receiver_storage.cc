#include "core/gateway/result_receiver_storage.hh"

#include "core/gateway/exact_state_tracker.hh"

namespace strij::gateway {

void ResultReceiverStorage::NotifyNodeDisconnected(const std::string& node_id) {
  std::vector<std::string> task_ids;
  for (const auto& [task_id, nid] : node_of_task_) {
    if (nid == node_id) {
      task_ids.push_back(task_id);
    }
  }

  for (const auto& task_id : task_ids) {
    auto* receiver = Get(task_id);
    if (receiver != nullptr) {
      receiver->DeliverError("node disconnected");
    }
    Erase(task_id);
    if (state_tracker_ != nullptr) {
      state_tracker_->RecordCompletion(task_id);
    }
  }
}

void ResultReceiverStorage::NotifyClientDisconnected(const std::string& task_id) {
  Erase(task_id);
  if (state_tracker_ != nullptr) {
    state_tracker_->RecordCompletion(task_id);
  }
}

} // namespace strij::gateway
