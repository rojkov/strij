#include "nodeagent/extensions/task_handlers/workflow/workflow_task_handler.hh"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"
#include "common/task/task.pb.h"
#include "common/core/utils/task_id.hh"
#include "strij/extensions/extension_registry.hh"
#include "strij/gateway/result_receiver_storage.hh"
#include "nodeagent/extensions/task_handlers/workflow/workflow.pb.h"

namespace strij::nodeagent::task_handlers {
namespace {

// Collects one child's outcome into the aggregation scratch owned by the
// synchronous HandleTask frame. The child executes on the same event-loop
// thread, so Deliver/DeliverError run before Submit() returns.
class WorkflowChildReceiver final : public gateway::ResultReceiver {
public:
  WorkflowChildReceiver(bool& errored, std::string& body, std::string& error)
      : errored_{errored}, body_{body}, error_{error} {}

  void Deliver(std::span<const std::byte> value, bool /*is_final*/) override {
    body_.append(reinterpret_cast<const char*>(value.data()), value.size());
  }

  void DeliverError(std::string_view reason) override {
    errored_ = true;
    error_ = std::string(reason);
  }

private:
  bool& errored_;
  std::string& body_;
  std::string& error_;
};

} // namespace

WorkflowTaskHandler::WorkflowTaskHandler(strij::nodeagent::ChildTaskSubmitter& submitter)
    : submitter_{submitter} {}

void WorkflowTaskHandler::HandleTask(const task::Task& task, ResultSenderPtr sender) {
  extensions::task_handlers::workflow::WorkflowPlan plan;
  std::string error_body;
  if (!plan.ParseFromArray(task.body().data(), static_cast<int>(task.body().size()))) {
    error_body = "malformed workflow plan";
  }

  std::string aggregate;
  for (const auto& child_spec : plan.children()) {
    const std::string child_id = utils::GenerateTaskId();
    task::Task child;
    child.set_id(child_id);
    child.set_type(child_spec.type());
    child.set_body(child_spec.body());
    if (child_spec.has_requirements()) {
      *child.mutable_requirements() = child_spec.requirements();
    }

    bool errored = false;
    std::string child_body;
    std::string child_error;
    auto receiver = std::make_unique<WorkflowChildReceiver>(errored, child_body, child_error);
    submitter_.Submit(std::move(child), std::move(receiver));
    if (errored) {
      error_body = "child '" + child_id + "' failed: " + child_error;
      break;
    }
    aggregate += child_body;
  }

  task::TaskResult result;
  result.set_id(task.id());
  if (!error_body.empty()) {
    result.set_body(error_body);
  } else {
    result.set_body(aggregate);
  }
  result.set_is_final(true);
  sender->Send(std::move(result));
}

auto WorkflowTaskHandlerFactory::Name() const -> std::string { return "workflow"; }

auto WorkflowTaskHandlerFactory::CreateEmptyConfigProto() -> MessagePtr {
  return std::make_unique<extensions::task_handlers::workflow::WorkflowTaskHandlerConfig>();
}

auto WorkflowTaskHandlerFactory::Create(const ::google::protobuf::Message& /*config*/,
                                        extensions::NodeagentFactoryContext& context) -> TaskHandlerPtr {
  return std::make_unique<WorkflowTaskHandler>(context.ChildTaskSubmitter());
}

auto WorkflowTaskHandlerFactory::ParseConfig(const ::google::protobuf::Message& config)
    -> absl::StatusOr<node::HandlerCapacity> {
  const auto* workflow_config =
      dynamic_cast<const extensions::task_handlers::workflow::WorkflowTaskHandlerConfig*>(&config);
  if (workflow_config == nullptr) {
    return absl::InvalidArgumentError("config is not a WorkflowTaskHandlerConfig");
  }
  return workflow_config->capacity();
}

} // namespace strij::nodeagent::task_handlers

REGISTER_FACTORY_FULLY_QUALIFIED(strij::nodeagent::task_handlers::WorkflowTaskHandlerFactory,
                                 strij::nodeagent::TaskHandlerFactory, workflow_task_handler_registrar)