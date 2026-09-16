#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

#include "nodeagent/core/local_receiver_registry.hh"
#include "nodeagent/extensions/task_handlers/task_handlers.hh"

namespace strij::nodeagent {

/**
 * @brief ResultSender bound to a fixed task id in the local receiver registry.
 *
 * Send(TaskResult) resolves the parent's receiver in LocalReceiverRegistry by
 * the bound task id, delivers `body`/`is_final`, and erases the registry entry
 * on the final result. A Send for a task whose registry entry is gone (e.g. an
 * already-cancelled parent, or a duplicate final) is dropped with a warning.
 *
 * Lifetime: the registry entry outlives the handler's Send calls (a handler is
 * created per task and may Send asynchronously). Lifecycle hooks
 * (RegisterOnClose) are accepted and stored for API symmetry with the
 * ConnectionResultSender, but never fire on their own: there is no connection
 * to tear down. A handler that must cancel its children should erase the
 * registry entries itself.
 */
class RegistryResultSender final : public nodeagent::ResultSender {
public:
  RegistryResultSender(LocalReceiverRegistry& registry, std::string task_id)
      : registry_{registry}, task_id_{std::move(task_id)} {}

  ~RegistryResultSender() override = default;

  RegistryResultSender(const RegistryResultSender&) = delete;
  auto operator=(const RegistryResultSender&) -> RegistryResultSender& = delete;
  RegistryResultSender(RegistryResultSender&&) noexcept = delete;
  auto operator=(RegistryResultSender&&) noexcept -> RegistryResultSender& = delete;

  void Send(task::TaskResult result) override;
  auto RegisterOnClose(std::move_only_function<void()> close_cb) -> std::size_t override;
  void UnregisterOnClose(std::size_t token) override;

private:
  LocalReceiverRegistry& registry_;
  std::string task_id_;
  std::unordered_map<std::size_t, std::move_only_function<void()>> close_callbacks_;
  std::size_t next_token_{0};
};

} // namespace strij::nodeagent