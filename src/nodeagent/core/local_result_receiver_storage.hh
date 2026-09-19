#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

#include "strij/gateway/result_receiver_storage.hh"

namespace strij::nodeagent {

// Node-side per-task result receiver storage (a node-local mirror of
// gateway::ResultReceiverStorage, keyed by task_id alone). Owned privately by
// the bundled "default" scheduler, which both registers (child-policy step in
// its Schedule) and resolves (its kResult/kTaskRejected frame handling)
// entries.
//
// The storage holds gateway::ResultReceiver instances delivered to it by
// the submission path (policy step in DefaultLocalScheduler::Schedule) and
// resolved by DefaultLocalScheduler's kResult/kTaskRejected cases. Entries are
// erased on final result, on rejection, or when a receiver is resolved
// directly via Get + Deliver.
class LocalResultReceiverStorage {
public:
  LocalResultReceiverStorage() = default;
  ~LocalResultReceiverStorage() = default;

  LocalResultReceiverStorage(const LocalResultReceiverStorage&) = delete;
  auto operator=(const LocalResultReceiverStorage&) -> LocalResultReceiverStorage& = delete;
  LocalResultReceiverStorage(LocalResultReceiverStorage&&) noexcept = delete;
  auto operator=(LocalResultReceiverStorage&&) noexcept -> LocalResultReceiverStorage& = delete;

  // Stores `receiver` under `task_id`, overwriting any prior entry.
  void Put(std::string task_id, gateway::ResultReceiverPtr receiver);

  // Returns a non-owning pointer to the receiver for `task_id`, or nullptr
  // when the id is unknown. The pointer is valid until the entry is erased.
  [[nodiscard]] auto Get(const std::string& task_id) -> gateway::ResultReceiver*;

  // Removes the receiver for `task_id` (if present). Returns true when an
  // entry was removed.
  auto Erase(const std::string& task_id) -> bool;

  [[nodiscard]] auto Empty() const -> bool;
  [[nodiscard]] auto Size() const -> size_t;

private:
  std::unordered_map<std::string, gateway::ResultReceiverPtr> receivers_;
};

} // namespace strij::nodeagent
