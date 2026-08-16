#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "absl/status/statusor.h"
#include "core/extensions/function_resolver.hh"
#include "core/node/capabilities.pb.h"
#include "extensions/task_handlers/task_handlers.hh"
#include "strij/event/command_handler.hh"
#include "strij/event/dispatcher.hh"

namespace strij::extensions::task_handlers {

class ChildProcess;

/**
 * @brief Runs a task by spawning the binary named by the `function` task
 * parameter, feeding the body on stdin and streaming stdout back chunk-by-chunk.
 *
 * Owns one ChildProcess per in-flight task, keyed by task id. Implements
 * event::CommandHandler so a ChildProcess can defer its own destruction through
 * a DEFERRED_DELETE command (the Connection::onEndOfStream pattern), keeping
 * teardown off the completion stack.
 */
class PipedExecutableTaskHandler final : public TaskHandler, public event::CommandHandler {
public:
  PipedExecutableTaskHandler(event::Dispatcher& dispatcher, FunctionResolver& resolver);

  // TaskHandler interface
  void HandleTask(const task::Task& task, ResultSenderPtr sender) override;

  // event::CommandHandler interface
  void ProcessCommand(event::Command cmd) override;

private:
  static void sendEmptyFinal(const task::Task& task, ResultSender& sender);

  event::Dispatcher& dispatcher_;
  FunctionResolver& resolver_;
  std::unordered_map<std::string, std::unique_ptr<ChildProcess>> children_;
};

class PipedExecutableTaskHandlerFactory final : public TaskHandlerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> TaskHandlerPtr override;
  auto ParseConfig(const ::google::protobuf::Message& config)
      -> absl::StatusOr<node::HandlerCapacity> override;
};

} // namespace strij::extensions::task_handlers
