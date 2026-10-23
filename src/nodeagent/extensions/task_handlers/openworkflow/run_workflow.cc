#include "nodeagent/extensions/task_handlers/openworkflow/run_workflow.hh"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/core/utils/task_id.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/interpreter.hh"
#include "openworkflow/document.hh"
#include "openworkflow/parser/parse.hh"
#include "openworkflow/task.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

using ::strij::openworkflow::Document;

auto ExtractEmbeddedWorkflow(const ::strij::openworkflow::RunBody& body)
    -> absl::StatusOr<EmbeddedWorkflow> {
  const auto* workflow = std::get_if<::strij::openworkflow::WorkflowProcess>(&body.process_);
  if (workflow == nullptr) {
    return absl::UnimplementedError(
        "only the workflow run process is supported in the current workflow slice");
  }

  if (workflow->namespace_ != kEmbeddedNamespace) {
    return absl::UnimplementedError(
        absl::StrCat("run.workflow '", workflow->name_, "' namespace '", workflow->namespace_,
                     "' is recognized but not supported; only '", kEmbeddedNamespace, "' is"));
  }

  if (body.return_.has_value()) {
    return absl::UnimplementedError(
        absl::StrCat("run.workflow '", workflow->name_,
                     "' return is recognized but not supported"));
  }

  if (!workflow->input_.has_value() || !workflow->input_->IsString()) {
    return absl::InvalidArgumentError(
        absl::StrCat("run.workflow '", workflow->name_,
                     "' input must be a string containing the embedded workflow document"));
  }

  return EmbeddedWorkflow{.definition_ = workflow->input_->AsString(), .name_ = workflow->name_};
}

auto ValidateEvaluateMode(const Document& document) -> absl::Status {
  if (document.evaluate_.has_value() && document.evaluate_->mode_.has_value()) {
    const std::string& mode = *document.evaluate_->mode_;
    if (mode != "strict") {
      return absl::InvalidArgumentError(
          absl::StrCat("evaluate mode '", mode, "' is not supported; only 'strict' is"));
    }
  }

  return absl::OkStatus();
}

auto ParseEmbeddedDefinition(std::string_view definition) -> absl::StatusOr<Document> {
  auto parsed = ::strij::openworkflow::Parse(definition);
  if (!parsed.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat("embedded workflow document did not parse: ", parsed.error().Message()));
  }

  if (absl::Status mode = ValidateEvaluateMode(*parsed); !mode.ok()) {
    return absl::InvalidArgumentError(absl::StrCat("embedded workflow: ", mode.message()));
  }

  return std::move(*parsed);
}

NestedWorkflowRunner::NestedWorkflowRunner(ChildTaskSubmitter& submitter,
                                           config::ExtensionConfig evaluator,
                                           std::size_t max_depth)
    : submitter_{submitter}, evaluator_{std::move(evaluator)}, max_depth_{max_depth} {}

NestedWorkflowRunner::~NestedWorkflowRunner() = default;

auto NestedWorkflowRunner::Start(std::string definition, std::string_view invocation_input,
                                 std::string child_id, std::size_t depth, Completion on_done)
    -> absl::Status {
  if (depth > max_depth_) {
    return absl::UnimplementedError(
        absl::StrCat("embedded workflow nesting depth ", depth, " exceeds the limit of ",
                     max_depth_));
  }

  const std::string key = child_id.empty() ? utils::GenerateTaskId() : std::move(child_id);
  auto interpreter = std::make_shared<WorkflowInterpreter>(
      submitter_, evaluator_, key,
      [this, key, on_done = std::move(on_done)](task::TaskResult result) mutable {
        if (on_done) {
          on_done(std::move(result));
        }

        finish(key);
      },
      kDefaultSynchronousIterationBudget, this, depth);

  runs_.insert_or_assign(key, NestedRun{.interpreter_ = interpreter});
  interpreter->Start(std::move(definition), invocation_input);

  // A run may have completed synchronously during `Start`. The local keeps its
  // interpreter alive, so dropping the reaped references here is safe.
  reaped_.clear();

  return absl::OkStatus();
}

void NestedWorkflowRunner::Cancel(std::string_view child_id) {
  auto node = runs_.extract(std::string(child_id));
  if (node.empty()) {
    return;
  }

  auto interpreter = std::move(node.mapped().interpreter_);
  interpreter->Cancel();
}

void NestedWorkflowRunner::finish(const std::string& key) {
  auto node = runs_.extract(key);
  if (node.empty()) {
    return;
  }

  // The completion callback is executing inside the interpreter, so keep it
  // alive until the runner reaches a safe point.
  reaped_.push_back(std::move(node.mapped().interpreter_));
}

} // namespace strij::nodeagent::task_handlers::openworkflow
