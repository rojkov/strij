#include "extensions/task_handlers/piped_executable/piped_executable_task_handler.hh"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "core/extensions/extension_registry.hh"
#include "core/extensions/function_resolver.hh"
#include "core/logging/log.hh"
#include "core/task/task.pb.h"
#include "extensions/task_handlers/piped_executable/child_process.hh"
#include "extensions/task_handlers/piped_executable/piped_executable.pb.h"

namespace strij::extensions::task_handlers {

PipedExecutableTaskHandler::PipedExecutableTaskHandler(event::Dispatcher& dispatcher,
                                                       extensions::FunctionResolver& resolver)
    : dispatcher_{dispatcher}, resolver_{resolver} {}

void PipedExecutableTaskHandler::HandleTask(const task::Task& task, ResultSenderPtr sender) {
  auto iter = task.parameters().find(std::string(kFunctionParameter));
  if (iter == task.parameters().end()) {
    LOG_WARNING("Task '{}': missing '{}' parameter; returning empty result", task.id(),
                kFunctionParameter);
    sendEmptyFinal(task, *sender);
    return;
  }

  auto resolved = resolver_.Resolve(iter->second);
  if (!resolved.ok()) {
    LOG_WARNING("Task '{}': failed to resolve '{}': {}; returning empty result", task.id(),
                iter->second, resolved.status().message());
    sendEmptyFinal(task, *sender);
    return;
  }
  std::string path = std::move(*resolved);

  std::vector<std::byte> stdin_body;
  stdin_body.reserve(task.body().size());
  for (const char chr : task.body()) {
    stdin_body.push_back(static_cast<std::byte>(chr));
  }

  auto child = std::make_unique<ChildProcess>(dispatcher_, this, task.id(), path,
                                              std::move(stdin_body), std::move(sender));
  if (child->Ok()) {
    child->Start();
    children_.emplace(task.id(), std::move(child));
  } else {
    // The ChildProcess delivered the empty final result during the failed
    // spawn, so nothing further is sent here.
    LOG_WARNING("Task '{}': failed to spawn '{}'; empty result sent", task.id(), path);
  }
}

void PipedExecutableTaskHandler::ProcessCommand(event::Command cmd) {
  if (cmd.type_ != event::Command::DEFERRED_DELETE || cmd.args_ == nullptr) {
    return;
  }
  auto* child = static_cast<ChildProcess*>(cmd.args_);
  for (auto it = children_.begin(); it != children_.end(); ++it) {
    if (it->second.get() == child) {
      children_.erase(it);
      return;
    }
  }
}

void PipedExecutableTaskHandler::sendEmptyFinal(const task::Task& task, ResultSender& sender) {
  task::TaskResult result;
  result.set_id(task.id());
  result.set_is_final(true);
  sender.Send(std::move(result));
}

auto PipedExecutableTaskHandlerFactory::Name() const -> std::string { return "piped_executable"; }

auto PipedExecutableTaskHandlerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<piped_executable::PipedExecutableTaskHandlerConfig>();
}

auto PipedExecutableTaskHandlerFactory::Create(const ::google::protobuf::Message& /*config*/,
                                               FactoryContext& context) -> TaskHandlerPtr {
  return std::make_unique<PipedExecutableTaskHandler>(context.Dispatcher(),
                                                      context.FunctionResolver());
}

auto PipedExecutableTaskHandlerFactory::ParseConfig(const ::google::protobuf::Message& config)
    -> absl::StatusOr<node::HandlerCapacity> {
  const auto* piped_config =
      dynamic_cast<const piped_executable::PipedExecutableTaskHandlerConfig*>(&config);
  if (piped_config == nullptr) {
    return absl::InvalidArgumentError("config is not a PipedExecutableTaskHandlerConfig");
  }
  return piped_config->capacity();
}

} // namespace strij::extensions::task_handlers

REGISTER_FACTORY_FULLY_QUALIFIED(
    strij::extensions::task_handlers::PipedExecutableTaskHandlerFactory,
    strij::extensions::TaskHandlerFactory, piped_executable_task_handler_registrar)
