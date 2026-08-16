#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "core/extensions/factory_context.hh"
#include "core/node/capabilities.pb.h"
#include "core/task/task.pb.h"
#include "google/protobuf/message.h"
#include "strij/common/pure.hh"

namespace strij::extensions {

/**
 * @brief Delivers task results back to the originating connection.
 *
 * HandleTask() transfers ownership of the sender to the handler, so it stays
 * valid until it is destroyed or the connection is torn down. A handler MAY
 * call Send() zero or more times, marking the final result is_final = true.
 * Sends made after connection teardown are dropped.
 *
 * RegisterOnClose() notifies the handler when the connection is torn down (so
 * an async handler can cancel in-flight work); handlers should call
 * UnregisterOnClose() once the callback is no longer needed. Callbacks and
 * Send() run on the event-loop thread.
 */
class ResultSender {
public:
  ResultSender() = default;
  virtual ~ResultSender() = default;

  ResultSender(const ResultSender&) = delete;
  auto operator=(const ResultSender&) -> ResultSender& = delete;
  ResultSender(ResultSender&&) noexcept = delete;
  auto operator=(ResultSender&&) noexcept -> ResultSender& = delete;

  virtual void Send(strij::task::TaskResult result) PURE;
  virtual auto RegisterOnClose(std::move_only_function<void()> close_cb) -> std::size_t PURE;
  virtual void UnregisterOnClose(std::size_t token) PURE;
};

using ResultSenderPtr = std::unique_ptr<ResultSender>;

/**
 * @brief Processes tasks of a specific type.
 *
 * Handler instances are shared across nodeagent connections, so handlers must
 * not store per-connection state. In-flight, per-task state (keyed by
 * task_id) is safe: frames and completions run on the single event-loop
 * thread. HandleTask() moves ownership of the ResultSender to the handler so
 * it can retain it for asynchronous, multi-shot delivery.
 */
class TaskHandler {
public:
  TaskHandler() = default;
  virtual ~TaskHandler() = default;

  TaskHandler(const TaskHandler&) = delete;
  auto operator=(const TaskHandler&) -> TaskHandler& = delete;
  TaskHandler(TaskHandler&&) noexcept = delete;
  auto operator=(TaskHandler&&) noexcept -> TaskHandler& = delete;

  virtual void HandleTask(const task::Task& task, ResultSenderPtr sender) PURE;
};

using TaskHandlerPtr = std::unique_ptr<TaskHandler>;

class TaskHandlerFactory {
public:
  using MessagePtr = std::unique_ptr<::google::protobuf::Message>;

  TaskHandlerFactory() = default;
  virtual ~TaskHandlerFactory() = default;

  TaskHandlerFactory(const TaskHandlerFactory&) = delete;
  auto operator=(const TaskHandlerFactory&) -> TaskHandlerFactory& = delete;
  TaskHandlerFactory(TaskHandlerFactory&&) noexcept = delete;
  auto operator=(TaskHandlerFactory&&) noexcept -> TaskHandlerFactory& = delete;

  [[nodiscard]] virtual auto Name() const -> std::string PURE;
  virtual auto CreateEmptyConfigProto() -> MessagePtr PURE;
  virtual auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> TaskHandlerPtr PURE;
  // Parses the operator-declared capacity (concurrency limit and default
  // resource requirements) out of the factory's config message. The default
  // implementation declares no concurrency limit and no default resources.
  [[nodiscard]] virtual auto ParseConfig(const ::google::protobuf::Message& /*config*/)
      -> absl::StatusOr<strij::node::HandlerCapacity> {
    return strij::node::HandlerCapacity{};
  }
};

} // namespace strij::extensions
