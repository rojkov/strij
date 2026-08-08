#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "core/extensions/function_resolver.hh"
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
  PipedExecutableTaskHandler(event::Dispatcher& dispatcher,
                             strij::extensions::FunctionResolver& resolver);
  ~PipedExecutableTaskHandler() override = default;

  PipedExecutableTaskHandler(const PipedExecutableTaskHandler&) = delete;
  auto operator=(const PipedExecutableTaskHandler&) -> PipedExecutableTaskHandler& = delete;
  PipedExecutableTaskHandler(PipedExecutableTaskHandler&&) noexcept = delete;
  auto operator=(PipedExecutableTaskHandler&&) noexcept -> PipedExecutableTaskHandler& = delete;

  // TaskHandler interface
  void HandleTask(const strij::task::Task& task, std::unique_ptr<ResultSender> sender) override;

  // event::CommandHandler interface
  void ProcessCommand(event::Command cmd) override;

private:
  void sendEmptyFinal(const strij::task::Task& task, ResultSender& sender);

  event::Dispatcher& dispatcher_;
  strij::extensions::FunctionResolver& resolver_;
  std::unordered_map<std::string, std::unique_ptr<ChildProcess>> children_;
};

class PipedExecutableTaskHandlerFactory final : public TaskHandlerFactory {
public:
  [[nodiscard]] auto Name() const -> std::string override;
  auto CreateEmptyConfigProto() -> MessagePtr override;
  auto Create(const ::google::protobuf::Message& config, FactoryContext& context)
      -> std::unique_ptr<TaskHandler> override;
};

} // namespace strij::extensions::task_handlers
