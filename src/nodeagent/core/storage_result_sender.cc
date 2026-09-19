#include "nodeagent/core/storage_result_sender.hh"

#include <span>
#include <string>

#include "common/core/logging/log.hh"
#include "common/task/task.pb.h"

namespace strij::nodeagent {

void StorageResultSender::Send(task::TaskResult result) {
  const bool is_final = !result.has_is_final() || result.is_final();
  const std::string& body = result.body();
  const auto data = std::as_bytes(std::span(body.data(), body.size()));

  gateway::ResultReceiver* receiver = storage_.Get(task_id_);
  if (receiver == nullptr) {
    LOG_WARNING("Result for unknown task '{}' dropped", task_id_);

    return;
  }

  receiver->Deliver(data, is_final);

  if (is_final) {
    storage_.Erase(task_id_);
  }
}

auto StorageResultSender::RegisterOnClose(std::move_only_function<void()> close_cb) -> std::size_t {
  const std::size_t token = next_token_++;
  close_callbacks_.insert_or_assign(token, std::move(close_cb));

  return token;
}

void StorageResultSender::UnregisterOnClose(std::size_t token) { close_callbacks_.erase(token); }

} // namespace strij::nodeagent