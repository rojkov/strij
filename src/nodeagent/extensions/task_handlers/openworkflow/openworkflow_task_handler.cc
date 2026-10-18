#include "nodeagent/extensions/task_handlers/openworkflow/openworkflow_task_handler.hh"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/artifact_loader.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/config.pb.h"
#include "strij/extensions/extension_registry.hh"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

auto loadFailureStatus(const absl::Status& status) -> task::TaskStatus {
  switch (status.code()) {
  case absl::StatusCode::kNotFound:
    return task::TASK_STATUS_NOT_FOUND;
  case absl::StatusCode::kInvalidArgument:
    return task::TASK_STATUS_MALFORMED_REQUEST;
  default:
    return task::TASK_STATUS_INTERNAL;
  }
}

void sendError(ResultSender& sender, const std::string& task_id, task::TaskStatus status,
               std::string_view message) {
  task::TaskResult result;
  result.set_id(task_id);
  result.set_status(status);
  result.set_body(std::string(message));
  result.set_is_final(true);
  sender.Send(std::move(result));
}

} // namespace

OpenWorkflowTaskHandler::OpenWorkflowTaskHandler(ChildTaskSubmitter& submitter,
                                                 FunctionResolver& resolver,
                                                 config::ExtensionConfig evaluator)
    : submitter_{submitter}, resolver_{resolver}, evaluator_{std::move(evaluator)} {}

void OpenWorkflowTaskHandler::HandleTask(const task::Task& task, ResultSenderPtr sender) {
  const std::string& task_id = task.id();
  // Defensive: a duplicate id supersedes any prior run for that id.
  runs_.erase(task_id);

  const auto function = task.parameters().find(std::string(kFunctionParameter));
  if (function == task.parameters().end()) {
    sendError(*sender, task_id, task::TASK_STATUS_MALFORMED_REQUEST,
              absl::StrCat("missing '", kFunctionParameter, "' parameter"));

    return;
  }

  auto resolved = resolver_.Resolve(function->second);
  if (!resolved.ok()) {
    sendError(*sender, task_id, task::TASK_STATUS_MALFORMED_REQUEST,
              absl::StrCat("failed to resolve function '", function->second,
                           "': ", resolved.status().message()));

    return;
  }

  auto document = LoadWorkflowArtifact(*resolved);
  if (!document.ok()) {
    sendError(*sender, task_id, loadFailureStatus(document.status()), document.status().message());

    return;
  }

  // The local owner keeps the interpreter alive through Start even if a
  // synchronous completion sends the result and reaps its own run.
  auto interpreter = std::make_shared<WorkflowInterpreter>(
      submitter_, evaluator_, task_id,
      [this, task_id](task::TaskResult result) { send(task_id, std::move(result)); });

  Run run;
  run.sender_ = std::move(sender);
  run.interpreter_ = interpreter;
  auto inserted = runs_.insert_or_assign(task_id, std::move(run));

  // Register after the run is installed: an already-closed mailbox fires the
  // callback immediately, which erases the run (and returns below).
  ResultSender* sender_ptr = inserted.first->second.sender_.get();
  inserted.first->second.close_token_ =
      sender_ptr->RegisterOnClose([this, task_id] { onClose(task_id); });

  if (!runs_.contains(task_id)) {
    return;
  }

  interpreter->Start(*document, task.body());
  reap();
}

void OpenWorkflowTaskHandler::reap() {
  // Erases runs whose interpreter has completed or been cancelled. Safe to call
  // from within a completion callback: the interpreter is additionally owned by
  // the caller (a HandleTask local or a receiver's locked weak_ptr).
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
  // The completion callback runs while its caller still owns the interpreter
  // (the HandleTask local, or the receiver's locked shared_ptr), so erasing the
  // finished run here does not free it mid-callback.
  reap();
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
                                                   deps.function_resolver_, std::move(evaluator));
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
