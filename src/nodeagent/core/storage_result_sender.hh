#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

#include "nodeagent/core/local_result_receiver_storage.hh"
#include "nodeagent/extensions/task_handlers/task_handlers.hh"

namespace strij::nodeagent {

/**
 * @brief ResultSender bound to a fixed task id in the local result-receiver
 * storage.
 *
 * Send(TaskResult) resolves the parent's receiver in LocalResultReceiverStorage
 * by the bound task id, delivers `body`/`is_final`, and erases the storage
 * entry on the final result. A Send for a task whose storage entry is gone
 * (e.g. an already-cancelled parent, or a duplicate final) is dropped with a
 * warning.
 *
 * Lifetime: the storage entry outlives the handler's Send calls (a handler is
 * created per task and may Send asynchronously). Lifecycle hooks
 * (RegisterOnClose) are accepted and stored for API symmetry with the
 * ConnectionResultSender, but never fire on their own: there is no connection
 * to tear down. A handler that must cancel its children should erase the
 * storage entries itself.
 */
class StorageResultSender final : public nodeagent::ResultSender {
public:
  StorageResultSender(LocalResultReceiverStorage& storage, std::string task_id)
      : storage_{storage}, task_id_{std::move(task_id)} {}

  ~StorageResultSender() override = default;

  StorageResultSender(const StorageResultSender&) = delete;
  auto operator=(const StorageResultSender&) -> StorageResultSender& = delete;
  StorageResultSender(StorageResultSender&&) noexcept = delete;
  auto operator=(StorageResultSender&&) noexcept -> StorageResultSender& = delete;

  void Send(task::TaskResult result) override;
  auto RegisterOnClose(std::move_only_function<void()> close_cb) -> std::size_t override;
  void UnregisterOnClose(std::size_t token) override;

private:
  LocalResultReceiverStorage& storage_;
  std::string task_id_;
  std::unordered_map<std::size_t, std::move_only_function<void()>> close_callbacks_;
  std::size_t next_token_{0};
};

} // namespace strij::nodeagent