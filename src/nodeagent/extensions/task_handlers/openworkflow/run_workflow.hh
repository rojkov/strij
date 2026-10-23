#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/config/extensions.pb.h"
#include "common/task/task.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/interpreter.hh"
#include "openworkflow/document.hh"
#include "openworkflow/task.hh"

namespace strij::nodeagent::task_handlers::openworkflow {

// The sentinel namespace for the embedded-definition profile. Any other
// namespace is recognized but unsupported until external resolution exists.
inline constexpr std::string_view kEmbeddedNamespace = "embedded";

// A generous bound on recursively embedded definitions. It is checked both in
// the pre-execution validation pass and by the nested runner.
inline constexpr std::size_t kDefaultEmbeddedNestingLimit = 32;

// The data-independent facts of an embedded `run.workflow` step.
struct EmbeddedWorkflow {
  std::string definition_;
  std::string name_;
};

// Extracts the embedded definition. `namespace` must be `embedded`, `input`
// must be a string holding the child document, and `return` must be absent.
// A non-workflow process kind is unimplemented, as is an external namespace.
// Errors are `InvalidArgument` for a malformed definition slot and
// `Unimplemented` for a recognized-but-unsupported shape.
auto ExtractEmbeddedWorkflow(const ::strij::openworkflow::RunBody& body)
    -> absl::StatusOr<EmbeddedWorkflow>;

// Parses an embedded definition and rejects a non-strict evaluate mode. Errors
// are `InvalidArgument`.
auto ParseEmbeddedDefinition(std::string_view definition)
    -> absl::StatusOr<::strij::openworkflow::Document>;

// Rejects any evaluate mode other than `strict`.
auto ValidateEvaluateMode(const ::strij::openworkflow::Document& document) -> absl::Status;

// Owns the interpreters started by embedded `run.workflow` steps. Awaited runs
// are keyed by the synthetic child id so the parent can cancel them; detached
// runs are keyed internally and survive their parent. The runner holds a
// non-owning reference back into the interpreter that started a run (through
// the completion callback), so it must outlive every run it owns.
class NestedWorkflowRunner {
public:
  using Completion = std::move_only_function<void(task::TaskResult)>;

  NestedWorkflowRunner(ChildTaskSubmitter& submitter, config::ExtensionConfig evaluator,
                       std::size_t max_depth = kDefaultEmbeddedNestingLimit);
  ~NestedWorkflowRunner();

  NestedWorkflowRunner(const NestedWorkflowRunner&) = delete;
  auto operator=(const NestedWorkflowRunner&) -> NestedWorkflowRunner& = delete;
  NestedWorkflowRunner(NestedWorkflowRunner&&) noexcept = delete;
  auto operator=(NestedWorkflowRunner&&) noexcept -> NestedWorkflowRunner& = delete;

  // Starts a nested interpreter over `definition`. A non-empty `child_id` marks
  // an awaited run and keys it for `Cancel`; an empty one is detached. `depth`
  // is the nesting level of the run being started (a top-level embedded run is
  // 1). `on_done` receives the nested result exactly once; detached runs pass
  // an empty completion.
  auto Start(std::string definition, std::string_view invocation_input, std::string child_id,
             std::size_t depth, Completion on_done) -> absl::Status;

  // Cancels an awaited run by child id. Unknown ids (including ordinary child
  // task ids) are ignored.
  void Cancel(std::string_view child_id);

  // The number of live runs owned by the runner.
  [[nodiscard]] auto Size() const -> std::size_t { return runs_.size(); }
  [[nodiscard]] auto MaxDepth() const -> std::size_t { return max_depth_; }

private:
  struct NestedRun {
    std::shared_ptr<WorkflowInterpreter> interpreter_;
  };

  // Moves a completed interpreter out of `runs_` without destroying it while its
  // own completion callback is on the stack.
  void finish(const std::string& key);

  ChildTaskSubmitter& submitter_;
  config::ExtensionConfig evaluator_;
  std::size_t max_depth_;
  std::map<std::string, NestedRun> runs_;
  std::vector<std::shared_ptr<WorkflowInterpreter>> reaped_;
};

} // namespace strij::nodeagent::task_handlers::openworkflow
