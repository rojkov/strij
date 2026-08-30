#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

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

// Pure-abstract contract for the gateway's per-task result receiver registry.
// Scheduler extensions reach it via GatewayFactoryContext::ResultReceiverStorage()
// and consume it through this interface rather than the concrete
// ResultReceiverStorageImpl.
class ResultReceiverStorage {
public:
  ResultReceiverStorage() = default;
  virtual ~ResultReceiverStorage() = default;

  ResultReceiverStorage(const ResultReceiverStorage&) = delete;
  auto operator=(const ResultReceiverStorage&) -> ResultReceiverStorage& = delete;
  ResultReceiverStorage(ResultReceiverStorage&&) noexcept = delete;
  auto operator=(ResultReceiverStorage&&) noexcept -> ResultReceiverStorage& = delete;

  virtual void Put(std::string task_id, ResultReceiverPtr receiver, std::string node_id) PURE;
  virtual auto Get(const std::string& task_id) -> ResultReceiver* PURE;
  virtual void Erase(const std::string& task_id) PURE;
  virtual auto Empty() const -> bool PURE;
  virtual auto Size() const -> size_t PURE;

  // Cleans up all receivers for tasks routed to `node_id`. Delivers errors to
  // still-connected HTTP clients, removes receivers, and records completions
  // in the state tracker.
  virtual void NotifyNodeDisconnected(const std::string& node_id) PURE;

  // Cleans up the receiver for a task whose HTTP client disconnected before
  // the task completed. Removes the receiver and records completion in the
  // state tracker so the node's in-flight accounting is unwound.
  virtual void NotifyClientDisconnected(const std::string& task_id) PURE;
};

} // namespace strij::gateway
