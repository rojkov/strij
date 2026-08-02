#pragma once

#include <memory>
#include <string>

#include "google/protobuf/message.h"

#include "carrot/common/pure.hh"
#include "core/extensions/factory_context.hh"
#include "core/extensions/extension_registry.hh"
#include "core/task/task.pb.h"

namespace carrot::extensions {

/**
 * @brief Delivers task results back to the originating connection.
 *
 * Today delivery is synchronous-only: the sender passed to HandleTask() is
 * valid only for the duration of that call, and a handler MUST NOT retain it
 * past HandleTask() (the concrete sender holds a live connection reference).
 * A synchronous handler calls Send() within HandleTask() and returns.
 *
 * Async/streaming delivery (a handler holding a sender and calling Send()
 * zero or more times later, marking the last result is_final = true) is the
 * future shape; it requires a connection-owned mailbox so senders outlive
 * HandleTask() and the connection teardown. That is a separate change.
 */
class ResultSender {
public:
  virtual ~ResultSender() = default;
  virtual void Send(carrot::task::TaskResult result) PURE;
};

/**
 * @brief Processes tasks of a specific type.
 *
 * Handler instances are shared across nodeagent connections, so handlers must
 * not store per-connection state. In-flight, per-task state (keyed by
 * task_id) is safe: frames and completions run on the single event-loop
 * thread.
 */
class TaskHandler {
public:
  virtual ~TaskHandler() = default;
  virtual void HandleTask(const carrot::task::Task& task, ResultSender& sender) PURE;
};

class TaskHandlerFactory {
public:
  using MessagePtr = std::unique_ptr<::google::protobuf::Message>;

  virtual ~TaskHandlerFactory() = default;
  virtual auto Name() const -> std::string PURE;
  virtual auto CreateEmptyConfigProto() -> MessagePtr PURE;
  virtual auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> std::unique_ptr<TaskHandler> PURE;
};

} // namespace carrot::extensions
