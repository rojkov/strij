#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "core/extensions/extension_registry.hh"
#include "core/extensions/factory_context.hh"
#include "core/task/task.pb.h"
#include "google/protobuf/message.h"
#include "strij/common/pure.hh"

namespace strij::extensions {

/**
 * @brief Delivers task results back to the originating connection.
 *
 * The sender is a copyable handle into the originating connection's outbound
 * mailbox. A handler MAY retain it past HandleTask(), call Send() zero or more
 * times, and mark the final result is_final = true. The sender stays valid
 * until the connection is torn down; sends made after teardown are dropped.
 *
 * RegisterOnClose() notifies the handler when the connection is torn down (so
 * an async handler can cancel in-flight work); handlers should call
 * UnregisterOnClose() once the callback is no longer needed. Callbacks and
 * Send() run on the event-loop thread.
 */
class ResultSender {
public:
  virtual ~ResultSender() = default;
  virtual void Send(strij::task::TaskResult result) PURE;
  virtual auto RegisterOnClose(std::move_only_function<void()> close_cb) -> std::size_t PURE;
  virtual void UnregisterOnClose(std::size_t token) PURE;
};

/**
 * @brief Processes tasks of a specific type.
 *
 * Handler instances are shared across nodeagent connections, so handlers must
 * not store per-connection state. In-flight, per-task state (keyed by
 * task_id) is safe: frames and completions run on the single event-loop
 * thread. A handler MAY retain the sender passed to HandleTask() past the
 * call (see ResultSender) for asynchronous, multi-shot delivery.
 */
class TaskHandler {
public:
  virtual ~TaskHandler() = default;
  virtual void HandleTask(const strij::task::Task& task, ResultSender& sender) PURE;
};

class TaskHandlerFactory {
public:
  using MessagePtr = std::unique_ptr<::google::protobuf::Message>;

  virtual ~TaskHandlerFactory() = default;
  [[nodiscard]] virtual auto Name() const -> std::string PURE;
  virtual auto CreateEmptyConfigProto() -> MessagePtr PURE;
  virtual auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> std::unique_ptr<TaskHandler> PURE;
};

} // namespace strij::extensions
