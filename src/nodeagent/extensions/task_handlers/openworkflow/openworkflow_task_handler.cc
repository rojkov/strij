#include "nodeagent/extensions/task_handlers/openworkflow/openworkflow_task_handler.hh"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/config.pb.h"
#include "strij/extensions/extension_registry.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

OpenWorkflowTaskHandler::OpenWorkflowTaskHandler(ChildTaskSubmitter& submitter,
                                                 config::ExtensionConfig evaluator)
    : submitter_{submitter}, evaluator_{std::move(evaluator)} {}

void OpenWorkflowTaskHandler::HandleTask(const task::Task& task, ResultSenderPtr sender) {
  const std::string& task_id = task.id();
  // Defensive: a duplicate id supersedes any prior run for that id.
  runs_.erase(task_id);

  auto interpreter = std::make_unique<WorkflowInterpreter>(
      submitter_, evaluator_, *this, task_id,
      [this, task_id](task::TaskResult result) { send(task_id, std::move(result)); });

  Run run;
  run.sender_ = std::move(sender);
  run.interpreter_ = std::move(interpreter);
  auto inserted = runs_.insert_or_assign(task_id, std::move(run));

  // Register after the run is installed: an already-closed mailbox fires the
  // callback immediately, which erases the run (and returns below).
  ResultSender* sender_ptr = inserted.first->second.sender_.get();
  inserted.first->second.close_token_ =
      sender_ptr->RegisterOnClose([this, task_id] { onClose(task_id); });

  auto iter = runs_.find(task_id);
  if (iter == runs_.end()) {
    return;
  }
  iter->second.interpreter_->Start(task.body());
  Reap();
}

void OpenWorkflowTaskHandler::OnChildOutcome(const std::string& parent_id,
                                             const std::string& child_id, ChildOutcome outcome) {
  auto iter = runs_.find(parent_id);
  if (iter == runs_.end()) {
    return;
  }
  iter->second.interpreter_->OnChildOutcome(child_id, std::move(outcome));
}

void OpenWorkflowTaskHandler::Reap() {
  for (auto iter = runs_.begin(); iter != runs_.end();) {
    Run& run = iter->second;
    const bool finished =
        run.finished_ || (run.interpreter_ != nullptr && run.interpreter_->Finished());
    if (!finished) {
      ++iter;
      continue;
    }
    if (run.close_token_.has_value() && run.sender_ != nullptr) {
      run.sender_->UnregisterOnClose(*run.close_token_);
    }
    iter = runs_.erase(iter);
  }
}

void OpenWorkflowTaskHandler::send(const std::string& task_id, task::TaskResult result) {
  auto iter = runs_.find(task_id);
  if (iter == runs_.end()) {
    return;
  }
  if (iter->second.sender_ != nullptr) {
    iter->second.sender_->Send(std::move(result));
  }
  if (iter->second.close_token_.has_value() && iter->second.sender_ != nullptr) {
    iter->second.sender_->UnregisterOnClose(*iter->second.close_token_);
    iter->second.close_token_.reset();
  }
  iter->second.finished_ = true;
}

void OpenWorkflowTaskHandler::onClose(const std::string& task_id) {
  auto iter = runs_.find(task_id);
  if (iter == runs_.end()) {
    return;
  }
  iter->second.interpreter_->Cancel();
  // The close callback lives in the connection's mailbox, not in the sender, so
  // erasing the run (and its sender) here is safe; it also avoids mutating the
  // mailbox's callback list while it is being iterated.
  runs_.erase(iter);
}

auto OpenWorkflowTaskHandlerFactory::Name() const -> std::string { return "openworkflow"; }

auto OpenWorkflowTaskHandlerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<extensions::task_handlers::openworkflow::OpenWorkflowTaskHandlerConfig>();
}

auto OpenWorkflowTaskHandlerFactory::Create(const ::google::protobuf::Message& config,
                                            const TaskHandlerDeps& deps) -> TaskHandlerPtr {
  const auto* openworkflow_config =
      dynamic_cast<const extensions::task_handlers::openworkflow::OpenWorkflowTaskHandlerConfig*>(
          &config);
  config::ExtensionConfig evaluator;
  if (openworkflow_config != nullptr && openworkflow_config->has_evaluator()) {
    evaluator = openworkflow_config->evaluator();
  } else {
    evaluator.set_name("jq");
  }
  return std::make_unique<OpenWorkflowTaskHandler>(deps.child_task_submitter_,
                                                   std::move(evaluator));
}

auto OpenWorkflowTaskHandlerFactory::ParseConfig(const ::google::protobuf::Message& config)
    -> absl::StatusOr<node::HandlerCapacity> {
  const auto* openworkflow_config =
      dynamic_cast<const extensions::task_handlers::openworkflow::OpenWorkflowTaskHandlerConfig*>(
          &config);
  if (openworkflow_config == nullptr) {
    return absl::InvalidArgumentError("config is not an OpenWorkflowTaskHandlerConfig");
  }
  return openworkflow_config->capacity();
}

} // namespace strij::nodeagent::task_handlers::openworkflow

REGISTER_FACTORY_FULLY_QUALIFIED(
    strij::nodeagent::task_handlers::openworkflow::OpenWorkflowTaskHandlerFactory,
    strij::nodeagent::TaskHandlerFactory, openworkflow_task_handler_registrar)
