#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "common/task/task.pb.h"
#include "gtest/gtest.h"
#include "nodeagent/extensions/task_handlers/openworkflow/frame.hh"
#include "openworkflow/document.hh"
#include "openworkflow/parser/parse.hh"
#include "openworkflow/task.hh"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

using ::strij::openworkflow::Document;
using ::strij::openworkflow::NamedTask;

// NOLINTBEGIN(modernize-use-trailing-return-type)

constexpr std::string_view kTwoNestedSequences = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: nested-sequences
  version: '0.1.0'
do:
  - first:
      do:
        - a:
            call: strij
            with:
              type: echo
  - second:
      do:
        - b:
            call: strij
            with:
              type: echo
)YAML";

constexpr std::string_view kTwoFlatSteps = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: flat
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
  - two:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kNestedDoValue = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: nested-value
  version: '0.1.0'
do:
  - outer:
      do:
        - a:
            call: strij
            with:
              type: echo
)YAML";

constexpr std::string_view kNestedDoForAbandon = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: nested-abandon
  version: '0.1.0'
do:
  - outer:
      do:
        - a:
            call: strij
            with:
              type: echo
        - b:
            call: strij
            with:
              type: echo
)YAML";

constexpr std::string_view kForkThreeBranches = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-all
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              call: strij
              with:
                type: echo
          - b:
              call: strij
              with:
                type: echo
          - c:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kForkCompete = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-compete
  version: '0.1.0'
do:
  - race:
      fork:
        compete: true
        branches:
          - fast:
              call: strij
              with:
                type: echo
          - slow:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kEmptyJoinAllFork = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: empty-all
  version: '0.1.0'
do:
  - fan:
      fork:
        branches: []
)YAML";

constexpr std::string_view kForkDirectFailure = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-direct-failure
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              call: strij
              with:
                type: echo
          - b:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kForkNestedDoFailure = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-nested-do-failure
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              do:
                - inner:
                    call: strij
                    with:
                      type: echo
          - b:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kForkNestedJoinFailure = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-nested-join-failure
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              fork:
                branches:
                  - leaf:
                      call: strij
                      with:
                        type: echo
)YAML";

constexpr std::string_view kTwoFlatStepsWithJump = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: jump
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
      then: three
  - two:
      call: strij
      with:
        type: echo
  - three:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kNestedExit = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: nested-exit
  version: '0.1.0'
do:
  - outer:
      do:
        - a:
            call: strij
            with:
              type: echo
            then: exit
        - b:
            call: strij
            with:
              type: echo
  - after:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kNestedEnd = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: nested-end
  version: '0.1.0'
do:
  - outer:
      export:
        as: '${ {outer: .} }'
      do:
        - a:
            call: strij
            with:
              type: echo
            then: end
        - b:
            call: strij
            with:
              type: echo
  - after:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kForkBranchEnd = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-end
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              call: strij
              with:
                type: echo
              then: end
          - b:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kDoThenExit = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: do-exit
  version: '0.1.0'
do:
  - outer:
      do:
        - a:
            call: strij
            with:
              type: echo
      then: exit
  - after:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kSkippedStepWithThen = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: skipped-then
  version: '0.1.0'
do:
  - skipped:
      if: '${ false }'
      call: strij
      with:
        type: echo
      then: third
  - second:
      call: strij
      with:
        type: echo
  - third:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kFailedStepWithThen = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: failed-then
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
      then: three
  - two:
      call: strij
      with:
        type: echo
  - three:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kSelfJumpingSet = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: self-loop
  version: '0.1.0'
do:
  - loop:
      set: '${ . }'
      then: loop
)YAML";

constexpr std::string_view kTwoTaskSetCycle = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: cycle
  version: '0.1.0'
do:
  - a:
      set: '${ . }'
      then: b
  - b:
      set: '${ . }'
      then: a
)YAML";

constexpr std::string_view kWideSynchronousFork = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: wide-fork
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              set: '${ . }'
          - b:
              set: '${ . }'
          - c:
              set: '${ . }'
          - d:
              set: '${ . }'
          - e:
              set: '${ . }'
          - f:
              set: '${ . }'
)YAML";

constexpr std::string_view kSuspendingSelfJump = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: async-loop
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
      then: one
)YAML";

constexpr std::string_view kForBodyCalls = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-body-calls
  version: '0.1.0'
do:
  - loop:
      for:
        each: color
        at: position
        in: '${ .colors }'
      do:
        - body:
            call: strij
            with:
              type: echo
)YAML";

constexpr std::string_view kForBodyWhile = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-body-while
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ .colors }'
      while: '${ true }'
      do:
        - body:
            call: strij
            with:
              type: echo
)YAML";

constexpr std::string_view kForBodyEnd = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-body-end
  version: '0.1.0'
do:
  - loop:
      for:
        in: '${ .colors }'
      do:
        - body:
            call: strij
            with:
              type: echo
            then: end
  - after:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kForNestedDo = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: for-nested-do
  version: '0.1.0'
do:
  - loop:
      for:
        each: color
        in: '${ .colors }'
      do:
        - outer:
            do:
              - a:
                  call: strij
                  with:
                    type: echo
)YAML";

constexpr std::string_view kAwaitedRun = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: awaited-run
  version: '0.1.0'
do:
  - subflow:
      run:
        workflow:
          namespace: embedded
          name: child
          version: '0.1.0'
          input: "document:\n  dsl: '1.0.3'\n  namespace: child\n  name: child\n  version: '0.1.0'\ndo:\n  - one:\n      set: {a: 1}\n"
      output:
        as: '${ . }'
)YAML";

constexpr std::string_view kDetachedRun = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: detached-run
  version: '0.1.0'
do:
  - subflow:
      run:
        await: false
        workflow:
          namespace: embedded
          name: child
          version: '0.1.0'
          input: "document:\n  dsl: '1.0.3'\n  namespace: child\n  name: child\n  version: '0.1.0'\ndo:\n  - one:\n      set: {a: 1}\n"
)YAML";

// Records calls, scripts inline/suspending/local-error outcomes, and lets a
// test deliver an asynchronous outcome to the sequence that suspended on it.
class FakeFrameContext final : public FrameContext {
public:
  enum class EffectKind : std::uint8_t { kInline = 0, kSuspend = 1, kLocalError = 2 };

  struct Effect {
    EffectKind kind_{EffectKind::kInline};
    std::string body_{"ok"};
    std::string child_id_;
    std::string message_;
  };

  struct ExportRecord {
    std::string step_;
    std::string input_;
    std::string output_;
  };

  void Script(Effect effect) { effects_.push_back(std::move(effect)); }

  void ScriptSuspend(std::string child_id) {
    Script(Effect{.kind_ = EffectKind::kSuspend, .child_id_ = std::move(child_id)});
  }

  void ScriptInline(std::string body) {
    Script(Effect{.kind_ = EffectKind::kInline, .body_ = std::move(body)});
  }

  auto EvaluateCondition(const NamedTask& step, const StepDataValue& /*raw*/,
                         const LexicalScope* scope) -> absl::StatusOr<bool> override {
    if (scope != nullptr) {
      scope_items_.insert_or_assign(step.name_, scope->item_.Dump());
    }

    return step.name_ != skipped_step_;
  }

  void SetSkippedStep(std::string name) { skipped_step_ = std::move(name); }

  auto TransformTaskInput(const NamedTask& /*step*/, const StepDataValue& raw,
                          const LexicalScope* /*scope*/) -> absl::StatusOr<StepDataValue> override {
    return raw.Copy();
  }

  auto TransformTaskOutput(const NamedTask& /*step*/, const StepDataValue& /*input*/,
                           const StepDataValue& raw_output, const LexicalScope* /*scope*/)
      -> absl::StatusOr<StepDataValue> override {
    return raw_output.Copy();
  }

  auto ExportTaskResult(const NamedTask& step, const StepDataValue& input,
                        const StepDataValue& transformed_output, const LexicalScope* /*scope*/)
      -> absl::Status override {
    exports_.push_back(ExportRecord{
        .step_ = step.name_, .input_ = input.Bytes(), .output_ = transformed_output.Bytes()});
    return absl::OkStatus();
  }

  auto EvaluateSet(const NamedTask& /*step*/, const StepDataValue& input,
                   const LexicalScope* /*scope*/) -> absl::StatusOr<StepDataValue> override {
    return input.Copy();
  }

  auto SelectSwitchDirective(const NamedTask& /*step*/, const StepDataValue& /*input*/,
                             const LexicalScope* /*scope*/)
      -> absl::StatusOr<std::optional<std::string>> override {
    return switch_directive_;
  }

  auto EvaluateForIn(const NamedTask& step, const StepDataValue& input,
                     const LexicalScope* /*scope*/) -> absl::StatusOr<StepDataValue> override {
    for_in_steps_.push_back(step.name_);
    for_in_inputs_.push_back(input.Bytes());
    return for_collection_.has_value() ? for_collection_->Copy() : StepDataValue::FromBytes("[]");
  }

  auto EvaluateForWhile(const NamedTask& step, const StepDataValue& accumulator,
                        const LexicalScope& scope) -> absl::StatusOr<bool> override {
    while_steps_.push_back(step.name_);
    while_accumulators_.push_back(accumulator.Bytes());
    while_scope_items_.push_back(scope.item_.Dump());

    if (!while_results_.empty()) {
      const bool result = while_results_.front();
      while_results_.pop_front();

      return result;
    }

    return while_default_;
  }

  void SetForIn(StepDataValue collection) { for_collection_ = std::move(collection); }
  void PushWhile(bool result) { while_results_.push_back(result); }
  void SetWhileDefault(bool result) { while_default_ = result; }

  void SetSwitchDirective(std::optional<std::string> directive) {
    switch_directive_ = std::move(directive);
  }

  auto ExecuteCall(SequenceFrame& owner, const NamedTask& step, const StepDataValue& /*input*/,
                   const LexicalScope* /*scope*/) -> absl::Status override {
    calls_.push_back(step.name_);

    return applyNextEffect(owner);
  }

  auto ExecuteRun(SequenceFrame& owner, const NamedTask& step, const StepDataValue& /*input*/,
                  const LexicalScope* /*scope*/) -> absl::Status override {
    runs_.push_back(step.name_);

    const auto& run_body = std::get<::strij::openworkflow::RunBody>(step.task_.body_);
    if (run_body.await_.has_value() && !*run_body.await_) {
      return absl::OkStatus();
    }

    return applyNextEffect(owner);
  }

  void ForgetChild(std::string_view child_id) override {
    forgotten_.push_back(std::string(child_id));
    waiting_.erase(std::string(child_id));
  }

  void Deliver(std::string_view child_id, ChildOutcome outcome) {
    auto waiting = waiting_.find(std::string(child_id));
    if (waiting == waiting_.end()) {
      return;
    }

    waiting->second->ApplyOutcome(std::move(outcome), *this);
    waiting_.erase(waiting);
  }

  [[nodiscard]] auto Calls() const -> const std::vector<std::string>& { return calls_; }
  [[nodiscard]] auto Runs() const -> const std::vector<std::string>& { return runs_; }
  [[nodiscard]] auto Forgotten() const -> const std::vector<std::string>& { return forgotten_; }
  [[nodiscard]] auto Exports() const -> const std::vector<ExportRecord>& { return exports_; }
  [[nodiscard]] auto ForInInputs() const -> const std::vector<std::string>& {
    return for_in_inputs_;
  }
  [[nodiscard]] auto WhileAccumulators() const -> const std::vector<std::string>& {
    return while_accumulators_;
  }
  [[nodiscard]] auto WhileScopeItems() const -> const std::vector<std::string>& {
    return while_scope_items_;
  }
  [[nodiscard]] auto ScopeItems() const -> const std::map<std::string, std::string>& {
    return scope_items_;
  }

  [[nodiscard]] static auto Success(std::string body) -> ChildOutcome {
    return ChildOutcome{.body_ = std::move(body), .reason_ = {}, .status_ = task::TASK_STATUS_OK};
  }

  [[nodiscard]] static auto Error(std::string reason, task::TaskStatus status) -> ChildOutcome {
    return ChildOutcome{.body_ = {}, .reason_ = std::move(reason), .status_ = status};
  }

private:
  auto applyNextEffect(SequenceFrame& owner) -> absl::Status {
    Effect effect;
    if (!effects_.empty()) {
      effect = std::move(effects_.front());
      effects_.pop_front();
    }

    switch (effect.kind_) {
    case EffectKind::kLocalError:
      return absl::InternalError(effect.message_);
    case EffectKind::kSuspend:
      owner.Suspend(effect.child_id_);
      waiting_.insert_or_assign(effect.child_id_, &owner);
      return absl::OkStatus();
    case EffectKind::kInline:
      owner.Suspend("inline-child");
      owner.ApplyOutcome(Success(std::move(effect.body_)), *this);
      return absl::OkStatus();
    }

    return absl::InternalError("unreachable");
  }

  std::deque<Effect> effects_;
  std::vector<std::string> calls_;
  std::vector<std::string> runs_;
  std::vector<std::string> forgotten_;
  std::vector<ExportRecord> exports_;
  std::map<std::string, SequenceFrame*> waiting_;
  std::optional<std::string> switch_directive_;
  std::string skipped_step_;
  std::optional<StepDataValue> for_collection_;
  std::deque<bool> while_results_;
  bool while_default_{true};
  std::vector<std::string> for_in_steps_;
  std::vector<std::string> for_in_inputs_;
  std::vector<std::string> while_steps_;
  std::vector<std::string> while_accumulators_;
  std::vector<std::string> while_scope_items_;
  std::map<std::string, std::string> scope_items_;
};

class LifecycleFrame final : public Frame {
public:
  void Enter(StepDataValue /*data*/) override { markActive(); }
  auto Advance(FrameContext& /*context*/) -> FrameResult override {
    markCompleted();
    return Completed{.value_ = StepDataValue{}};
  }
  void Abandon(FrameContext& /*context*/) override { markAbandoned(); }
  void Fail() { markFailed(); }
};

auto MakeRoot(const Document& document) -> std::unique_ptr<SequenceFrame> {
  auto root = CreateRootFrame(document);
  root->Enter(StepDataValue::FromBytes("null"));

  return root;
}

// NOLINTBEGIN(bugprone-unchecked-optional-access)

TEST(FrameLifecycleTest, AbandonIsANoOpOnACompletedFrame) {
  FakeFrameContext context;
  LifecycleFrame frame;

  ASSERT_TRUE(frame.IsPending());
  frame.Enter(StepDataValue{});
  EXPECT_TRUE(frame.IsActive());

  auto result = frame.Advance(context);
  EXPECT_TRUE(std::holds_alternative<Completed>(result));
  ASSERT_TRUE(frame.IsCompleted());

  frame.Abandon(context);
  EXPECT_TRUE(frame.IsCompleted());
  EXPECT_EQ(frame.Lifecycle(), Frame::State::kCompleted);
}

TEST(FrameLifecycleTest, AbandonIsANoOpOnAFailedFrame) {
  FakeFrameContext context;
  LifecycleFrame frame;

  frame.Enter(StepDataValue{});
  frame.Fail();
  ASSERT_TRUE(frame.IsFailed());

  frame.Abandon(context);
  EXPECT_TRUE(frame.IsFailed());
  EXPECT_EQ(frame.Lifecycle(), Frame::State::kFailed);
}

TEST(FrameLifecycleTest, ActiveFrameBecomesAbandoned) {
  FakeFrameContext context;
  LifecycleFrame frame;

  frame.Enter(StepDataValue{});
  frame.Abandon(context);
  EXPECT_TRUE(frame.IsAbandoned());
}

TEST(SequenceFrameTest, MaterializesOnlyTheCurrentNestedStep) {
  auto parsed = ::strij::openworkflow::Parse(kTwoNestedSequences);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptSuspend("child-a");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));
  EXPECT_EQ(root->Cursor(), 0U);
  ASSERT_NE(root->Child(), nullptr);

  const auto* first = dynamic_cast<const SequenceFrame*>(root->Child());
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->Cursor(), 0U);
  EXPECT_EQ(first->Child(), nullptr);
  EXPECT_EQ(context.Calls(), std::vector<std::string>{"a"});

  context.Deliver("child-a", FakeFrameContext::Success("1"));
  context.ScriptSuspend("child-b");

  result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));
  EXPECT_EQ(root->Cursor(), 1U);
  ASSERT_NE(root->Child(), nullptr);

  const auto* second = dynamic_cast<const SequenceFrame*>(root->Child());
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->Cursor(), 0U);
  ASSERT_EQ(second->Steps().size(), 1U);
  EXPECT_EQ(second->Steps()[0].name_, "b");
  EXPECT_EQ(context.Calls(), (std::vector<std::string>{"a", "b"}));
}

TEST(SequenceFrameTest, CompletesTwoStepsOneAtATime) {
  auto parsed = ::strij::openworkflow::Parse(kTwoFlatSteps);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptSuspend("child-one");
  context.ScriptSuspend("child-two");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));
  EXPECT_EQ(root->Cursor(), 0U);
  EXPECT_TRUE(root->IsWaiting());

  context.Deliver("child-one", FakeFrameContext::Success("1"));
  EXPECT_EQ(root->Cursor(), 1U);
  EXPECT_FALSE(root->IsWaiting());

  result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));
  EXPECT_EQ(root->Cursor(), 1U);

  context.Deliver("child-two", FakeFrameContext::Success("2"));
  result = root->Advance(context);
  auto* completed = std::get_if<Completed>(&result);
  ASSERT_NE(completed, nullptr);
  EXPECT_EQ(completed->value_.Bytes(), "2");
  EXPECT_TRUE(root->IsCompleted());
}

TEST(SequenceFrameTest, NestedDoReturnsItsValueToTheParent) {
  auto parsed = ::strij::openworkflow::Parse(kNestedDoValue);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptInline("42");

  auto result = root->Advance(context);
  auto* completed = std::get_if<Completed>(&result);
  ASSERT_NE(completed, nullptr);
  EXPECT_EQ(completed->value_.Bytes(), "42");
  EXPECT_EQ(context.Calls(), std::vector<std::string>{"a"});
}

TEST(SequenceFrameTest, AsyncSuccessResumesAndAsyncErrorBecomesFailed) {
  auto parsed = ::strij::openworkflow::Parse(kTwoFlatSteps);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptSuspend("child-one");
  context.ScriptSuspend("child-two");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));

  context.Deliver("child-one", FakeFrameContext::Success("resumed"));
  EXPECT_EQ(root->Cursor(), 1U);
  EXPECT_FALSE(root->IsWaiting());

  result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));

  context.Deliver("child-two", FakeFrameContext::Error("boom", task::TASK_STATUS_CAPACITY_REFUSED));
  EXPECT_TRUE(root->IsFailed());

  result = root->Advance(context);
  auto* failed = std::get_if<Failed>(&result);
  ASSERT_NE(failed, nullptr);
  EXPECT_EQ(failed->failure_.status_, task::TASK_STATUS_CAPACITY_REFUSED);
  EXPECT_EQ(failed->failure_.origin_, root.get());
  EXPECT_EQ(failed->failure_.step_, "two");
  EXPECT_EQ(failed->failure_.message_, "boom");
  EXPECT_FALSE(failed->failure_.composed_);
}

TEST(SequenceFrameTest, AbandonForgetsTheWaitingChildAndKeepsTheTreeAlive) {
  auto parsed = ::strij::openworkflow::Parse(kNestedDoForAbandon);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptSuspend("child-a");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));

  const Frame* nested = root->Child();
  ASSERT_NE(nested, nullptr);

  root->Abandon(context);

  EXPECT_TRUE(root->IsAbandoned());
  EXPECT_TRUE(nested->IsAbandoned());
  EXPECT_EQ(root->Child(), nested);
  ASSERT_EQ(context.Forgotten().size(), 1U);
  EXPECT_EQ(context.Forgotten()[0], "child-a");
}

TEST(ForkFrameTest, JoinAllCollectsResultsInDeclarationOrder) {
  auto parsed = ::strij::openworkflow::Parse(kForkThreeBranches);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptSuspend("child-a");
  context.ScriptSuspend("child-b");
  context.ScriptSuspend("child-c");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));
  EXPECT_EQ(context.Calls(), (std::vector<std::string>{"a", "b", "c"}));

  context.Deliver("child-c", FakeFrameContext::Success("3"));
  result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));

  context.Deliver("child-a", FakeFrameContext::Success("1"));
  context.Deliver("child-b", FakeFrameContext::Success("2"));

  result = root->Advance(context);
  auto* completed = std::get_if<Completed>(&result);
  ASSERT_NE(completed, nullptr);
  EXPECT_EQ(completed->value_.Bytes(), "[1,2,3]");
}

TEST(ForkFrameTest, CompeteResolvesWithTheFirstTerminalBranch) {
  auto parsed = ::strij::openworkflow::Parse(kForkCompete);
  ASSERT_TRUE(parsed.has_value());
  const auto& fork_body = std::get<::strij::openworkflow::ForkBody>(parsed->do_[0].task_.body_);
  ForkFrame fork(std::span<const NamedTask>(fork_body.branches_), fork_body.compete_);
  fork.Enter(StepDataValue::FromBytes("null"));

  FakeFrameContext context;
  context.ScriptSuspend("child-fast");
  context.ScriptSuspend("child-slow");

  auto result = fork.Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));
  EXPECT_EQ(context.Calls(), (std::vector<std::string>{"fast", "slow"}));

  context.Deliver("child-slow", FakeFrameContext::Success("7"));

  result = fork.Advance(context);
  auto* completed = std::get_if<Completed>(&result);
  ASSERT_NE(completed, nullptr);
  EXPECT_EQ(completed->value_.Bytes(), "7");

  ASSERT_EQ(fork.Branches().size(), 2U);
  EXPECT_TRUE(fork.Branches()[0]->IsAbandoned());
  EXPECT_TRUE(fork.Branches()[1]->IsCompleted());
}

TEST(ForkFrameTest, EmptyJoinAllForkJoinsToTheEmptyArray) {
  auto parsed = ::strij::openworkflow::Parse(kEmptyJoinAllFork);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  auto result = root->Advance(context);
  auto* completed = std::get_if<Completed>(&result);
  ASSERT_NE(completed, nullptr);
  EXPECT_EQ(completed->value_.Bytes(), "[]");
  EXPECT_TRUE(context.Calls().empty());
}

TEST(ForkFrameTest, DirectBranchFailureOmitsTheStepName) {
  auto parsed = ::strij::openworkflow::Parse(kForkDirectFailure);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptSuspend("child-a");
  context.ScriptSuspend("child-b");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));

  context.Deliver("child-a", FakeFrameContext::Error("boom", task::TASK_STATUS_UNAVAILABLE));

  result = root->Advance(context);
  auto* failed = std::get_if<Failed>(&result);
  ASSERT_NE(failed, nullptr);
  EXPECT_TRUE(failed->failure_.composed_);
  EXPECT_EQ(failed->failure_.message_, "branch 'a': boom");
  EXPECT_TRUE(context.Forgotten().size() == 1U);
  EXPECT_EQ(context.Forgotten()[0], "child-b");
}

TEST(ForkFrameTest, NestedDoBranchFailureNamesTheInnerStep) {
  auto parsed = ::strij::openworkflow::Parse(kForkNestedDoFailure);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptSuspend("child-inner");
  context.ScriptSuspend("child-b");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));
  EXPECT_EQ(context.Calls(), (std::vector<std::string>{"inner", "b"}));

  context.Deliver("child-inner", FakeFrameContext::Error("boom", task::TASK_STATUS_UNAVAILABLE));

  result = root->Advance(context);
  auto* failed = std::get_if<Failed>(&result);
  ASSERT_NE(failed, nullptr);
  EXPECT_TRUE(failed->failure_.composed_);
  EXPECT_EQ(failed->failure_.message_, "branch 'a': step 'inner': boom");
}

TEST(ForkFrameTest, NestedJoinFailureIsNamedOnlyByTheInnerBranch) {
  auto parsed = ::strij::openworkflow::Parse(kForkNestedJoinFailure);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptSuspend("child-leaf");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));

  context.Deliver("child-leaf", FakeFrameContext::Success("not json"));

  result = root->Advance(context);
  auto* failed = std::get_if<Failed>(&result);
  ASSERT_NE(failed, nullptr);
  EXPECT_TRUE(failed->failure_.composed_);
  EXPECT_EQ(failed->failure_.message_, "branch 'leaf': the branch result is not a JSON value");
}

TEST(FlowDirectiveTest, ResolvesReservedAndNamedTargets) {
  const std::vector<NamedTask> scope{NamedTask{.name_ = "first"}, NamedTask{.name_ = "second"}};
  const std::span<const NamedTask> span(scope);

  auto continue_directive = ResolveFlowDirective(span, "continue");
  ASSERT_TRUE(continue_directive.ok());
  EXPECT_EQ(continue_directive->kind_, FlowKind::kContinue);

  auto absent = ResolveFlowDirective(span, "");
  ASSERT_TRUE(absent.ok());
  EXPECT_EQ(absent->kind_, FlowKind::kContinue);

  auto exit_directive = ResolveFlowDirective(span, "exit");
  ASSERT_TRUE(exit_directive.ok());
  EXPECT_EQ(exit_directive->kind_, FlowKind::kExit);

  auto end_directive = ResolveFlowDirective(span, "end");
  ASSERT_TRUE(end_directive.ok());
  EXPECT_EQ(end_directive->kind_, FlowKind::kEnd);

  auto forward = ResolveFlowDirective(span, "second");
  ASSERT_TRUE(forward.ok());
  EXPECT_EQ(forward->kind_, FlowKind::kJump);
  EXPECT_EQ(forward->target_, 1U);

  auto backward = ResolveFlowDirective(span, "first");
  ASSERT_TRUE(backward.ok());
  EXPECT_EQ(backward->kind_, FlowKind::kJump);
  EXPECT_EQ(backward->target_, 0U);

  EXPECT_FALSE(ResolveFlowDirective(span, "missing").ok());
}

TEST(SequenceFrameFlowTest, ExitCompletesTheNestedSequenceAndContinues) {
  auto parsed = ::strij::openworkflow::Parse(kNestedExit);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptSuspend("child-a");
  context.ScriptSuspend("child-after");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));

  context.Deliver("child-a", FakeFrameContext::Success("1"));
  result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));
  EXPECT_EQ(context.Calls(), (std::vector<std::string>{"a", "after"}));
  EXPECT_FALSE(root->IsFailed());
}

TEST(SequenceFrameFlowTest, EndSkipsTheEnclosingDoPipeline) {
  auto parsed = ::strij::openworkflow::Parse(kNestedEnd);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptSuspend("child-a");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));

  context.Deliver("child-a", FakeFrameContext::Success("1"));
  result = root->Advance(context);

  auto* ended = std::get_if<Ended>(&result);
  ASSERT_NE(ended, nullptr);
  EXPECT_EQ(ended->value_.Bytes(), "1");
  EXPECT_EQ(context.Calls(), std::vector<std::string>{"a"});
  for (const auto& export_record : context.Exports()) {
    EXPECT_NE(export_record.step_, "outer");
  }
}

TEST(SequenceFrameFlowTest, EndInAForkBranchAbandonsItsSiblings) {
  auto parsed = ::strij::openworkflow::Parse(kForkBranchEnd);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptSuspend("child-a");
  context.ScriptSuspend("child-b");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));
  EXPECT_EQ(context.Calls(), (std::vector<std::string>{"a", "b"}));

  context.Deliver("child-a", FakeFrameContext::Success("1"));
  result = root->Advance(context);

  auto* ended = std::get_if<Ended>(&result);
  ASSERT_NE(ended, nullptr);
  EXPECT_EQ(ended->value_.Bytes(), "1");
  ASSERT_EQ(context.Forgotten().size(), 1U);
  EXPECT_EQ(context.Forgotten()[0], "child-b");
}

TEST(SequenceFrameFlowTest, ContainerDirectiveExitCompletesTheSequence) {
  auto parsed = ::strij::openworkflow::Parse(kDoThenExit);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptInline("1");

  auto result = root->Advance(context);
  auto* completed = std::get_if<Completed>(&result);
  ASSERT_NE(completed, nullptr);
  EXPECT_EQ(completed->value_.Bytes(), "1");
  EXPECT_EQ(context.Calls(), std::vector<std::string>{"a"});
  EXPECT_TRUE(root->IsCompleted());
}

TEST(SequenceFrameFlowTest, AResumedCallRunsItsJumpDirective) {
  auto parsed = ::strij::openworkflow::Parse(kTwoFlatStepsWithJump);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptSuspend("child-one");
  context.ScriptSuspend("child-three");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));

  context.Deliver("child-one", FakeFrameContext::Success("1"));
  EXPECT_EQ(root->Cursor(), 2U);

  result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));
  EXPECT_EQ(context.Calls(), (std::vector<std::string>{"one", "three"}));
}

TEST(SequenceFrameFlowTest, AnInlineCallRunsItsJumpDirective) {
  auto parsed = ::strij::openworkflow::Parse(kTwoFlatStepsWithJump);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptInline("1");
  context.ScriptSuspend("child-three");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));
  EXPECT_EQ(context.Calls(), (std::vector<std::string>{"one", "three"}));
}

TEST(SequenceFrameFlowTest, ASkippedStepDoesNotRunItsDirective) {
  auto parsed = ::strij::openworkflow::Parse(kSkippedStepWithThen);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.SetSkippedStep("skipped");
  context.ScriptSuspend("child-second");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));
  EXPECT_EQ(context.Calls(), std::vector<std::string>{"second"});
}

TEST(SequenceFrameFlowTest, AFailedStepDoesNotRunItsDirective) {
  auto parsed = ::strij::openworkflow::Parse(kFailedStepWithThen);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.Script(FakeFrameContext::Effect{.kind_ = FakeFrameContext::EffectKind::kLocalError,
                                          .message_ = "boom"});

  auto result = root->Advance(context);
  auto* failed = std::get_if<Failed>(&result);
  ASSERT_NE(failed, nullptr);
  EXPECT_EQ(failed->failure_.status_, task::TASK_STATUS_INTERNAL);
  EXPECT_EQ(context.Calls(), std::vector<std::string>{"one"});
}

TEST(SequenceFrameFlowTest, TheSynchronousGuardStopsASelfJump) {
  auto parsed = ::strij::openworkflow::Parse(kSelfJumpingSet);
  ASSERT_TRUE(parsed.has_value());
  SequenceFrame root(std::span<const NamedTask>(parsed->do_), std::nullopt, 5);
  root.Enter(StepDataValue::FromBytes("null"));

  FakeFrameContext context;
  auto result = root.Advance(context);

  auto* failed = std::get_if<Failed>(&result);
  ASSERT_NE(failed, nullptr);
  EXPECT_EQ(failed->failure_.status_, task::TASK_STATUS_INTERNAL);
  EXPECT_EQ(failed->failure_.step_, "loop");
  EXPECT_NE(failed->failure_.message_.find("budget"), std::string::npos);
}

TEST(SequenceFrameFlowTest, TheSynchronousGuardStopsATwoTaskCycle) {
  auto parsed = ::strij::openworkflow::Parse(kTwoTaskSetCycle);
  ASSERT_TRUE(parsed.has_value());
  SequenceFrame root(std::span<const NamedTask>(parsed->do_), std::nullopt, 7);
  root.Enter(StepDataValue::FromBytes("null"));

  FakeFrameContext context;
  auto result = root.Advance(context);

  auto* failed = std::get_if<Failed>(&result);
  ASSERT_NE(failed, nullptr);
  EXPECT_EQ(failed->failure_.status_, task::TASK_STATUS_INTERNAL);
}

TEST(SequenceFrameFlowTest, AWideSynchronousForkDoesNotTripTheGuard) {
  auto parsed = ::strij::openworkflow::Parse(kWideSynchronousFork);
  ASSERT_TRUE(parsed.has_value());
  SequenceFrame root(std::span<const NamedTask>(parsed->do_), std::nullopt, 2);
  root.Enter(StepDataValue::FromBytes("null"));

  FakeFrameContext context;
  auto result = root.Advance(context);

  EXPECT_TRUE(std::holds_alternative<Completed>(result));
  EXPECT_FALSE(root.IsFailed());
}

TEST(SequenceFrameFlowTest, ASuspendingLoopResetsTheSynchronousGuard) {
  auto parsed = ::strij::openworkflow::Parse(kSuspendingSelfJump);
  ASSERT_TRUE(parsed.has_value());
  SequenceFrame root(std::span<const NamedTask>(parsed->do_), std::nullopt, 2);
  root.Enter(StepDataValue::FromBytes("null"));

  FakeFrameContext context;
  context.ScriptSuspend("child-0");
  auto result = root.Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));

  for (int iteration = 1; iteration <= 10; ++iteration) {
    context.Deliver("child-" + std::to_string(iteration - 1), FakeFrameContext::Success("1"));
    context.ScriptSuspend("child-" + std::to_string(iteration));
    result = root.Advance(context);
    ASSERT_TRUE(std::holds_alternative<Suspended>(result));
    EXPECT_FALSE(root.IsFailed());
  }
}

TEST(ForFrameTest, IteratesInOrderAndCarriesTheAccumulator) {
  auto parsed = ::strij::openworkflow::Parse(kForBodyCalls);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.SetForIn(StepDataValue::FromBytes(R"(["red","green","blue"])"));
  context.ScriptInline("1");
  context.ScriptInline("2");
  context.ScriptInline("3");

  auto result = root->Advance(context);
  auto* completed = std::get_if<Completed>(&result);
  ASSERT_NE(completed, nullptr);
  EXPECT_EQ(completed->value_.Bytes(), "3");
  EXPECT_EQ(context.Calls(), (std::vector<std::string>{"body", "body", "body"}));
  ASSERT_EQ(context.ForInInputs().size(), 1U);
  EXPECT_EQ(context.ForInInputs()[0], "null");
}

TEST(ForFrameTest, EmptyAndNullCollectionsAreIdentities) {
  for (std::string_view collection : {"[]", "null"}) {
    auto parsed = ::strij::openworkflow::Parse(kForBodyCalls);
    ASSERT_TRUE(parsed.has_value());
    auto root = MakeRoot(*parsed);

    FakeFrameContext context;
    context.SetForIn(StepDataValue::FromBytes(std::string(collection)));

    auto result = root->Advance(context);
    auto* completed = std::get_if<Completed>(&result);
    ASSERT_NE(completed, nullptr);
    EXPECT_EQ(completed->value_.Bytes(), "null");
    EXPECT_TRUE(context.Calls().empty());
  }
}

TEST(ForFrameTest, ASuspendedIterationResumesWithTheSameBody) {
  auto parsed = ::strij::openworkflow::Parse(kForBodyCalls);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.SetForIn(StepDataValue::FromBytes(R"(["red","green"])"));
  context.ScriptSuspend("child-0");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));

  context.Deliver("child-0", FakeFrameContext::Success("10"));
  context.ScriptSuspend("child-1");
  result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));
  EXPECT_EQ(context.Calls(), (std::vector<std::string>{"body", "body"}));

  context.Deliver("child-1", FakeFrameContext::Success("20"));
  result = root->Advance(context);
  auto* completed = std::get_if<Completed>(&result);
  ASSERT_NE(completed, nullptr);
  EXPECT_EQ(completed->value_.Bytes(), "20");
}

TEST(ForFrameTest, WhileFalseStopsBeforeTheNextElement) {
  auto parsed = ::strij::openworkflow::Parse(kForBodyWhile);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.SetForIn(StepDataValue::FromBytes(R"(["red","green","blue"])"));
  context.SetWhileDefault(false);
  context.ScriptInline("1");
  context.ScriptInline("2");

  auto result = root->Advance(context);
  auto* completed = std::get_if<Completed>(&result);
  ASSERT_NE(completed, nullptr);
  EXPECT_EQ(completed->value_.Bytes(), "1");
  EXPECT_EQ(context.Calls(), std::vector<std::string>{"body"});
  ASSERT_EQ(context.WhileAccumulators().size(), 1U);
  EXPECT_EQ(context.WhileAccumulators()[0], "1");
  ASSERT_EQ(context.WhileScopeItems().size(), 1U);
  EXPECT_EQ(context.WhileScopeItems()[0], R"("red")");
}

TEST(ForFrameTest, EndInsideTheBodyStopsTheWholeWorkflow) {
  auto parsed = ::strij::openworkflow::Parse(kForBodyEnd);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.SetForIn(StepDataValue::FromBytes(R"(["red","green"])"));
  context.ScriptInline("1");

  auto result = root->Advance(context);
  auto* ended = std::get_if<Ended>(&result);
  ASSERT_NE(ended, nullptr);
  EXPECT_EQ(ended->value_.Bytes(), "1");
  EXPECT_EQ(context.Calls(), std::vector<std::string>{"body"});
}

TEST(ForFrameTest, ABodyFailurePropagatesAndKeepsTheBodyAlive) {
  auto parsed = ::strij::openworkflow::Parse(kForBodyCalls);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.SetForIn(StepDataValue::FromBytes(R"(["red"])"));
  context.Script(FakeFrameContext::Effect{.kind_ = FakeFrameContext::EffectKind::kLocalError,
                                          .message_ = "boom"});

  auto result = root->Advance(context);
  auto* failed = std::get_if<Failed>(&result);
  ASSERT_NE(failed, nullptr);
  EXPECT_EQ(failed->failure_.status_, task::TASK_STATUS_INTERNAL);
  EXPECT_EQ(failed->failure_.step_, "body");
  EXPECT_EQ(failed->failure_.message_, "boom");

  const auto* loop = dynamic_cast<const ForFrame*>(root->Child());
  ASSERT_NE(loop, nullptr);
  EXPECT_NE(loop->BodyFrame(), nullptr);
}

TEST(ForFrameTest, AbandonForgetsTheCurrentIterationsChild) {
  auto parsed = ::strij::openworkflow::Parse(kForBodyCalls);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.SetForIn(StepDataValue::FromBytes(R"(["red","green"])"));
  context.ScriptSuspend("child-0");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));

  const auto* loop = dynamic_cast<const ForFrame*>(root->Child());
  ASSERT_NE(loop, nullptr);
  root->Abandon(context);

  EXPECT_TRUE(loop->IsAbandoned());
  ASSERT_EQ(context.Forgotten().size(), 1U);
  EXPECT_EQ(context.Forgotten()[0], "child-0");
}

TEST(ForFrameTest, TheSynchronousIterationBudgetStopsALargeLoop) {
  auto parsed = ::strij::openworkflow::Parse(kForBodyCalls);
  ASSERT_TRUE(parsed.has_value());
  const auto& body = std::get<::strij::openworkflow::ForBody>(parsed->do_[0].task_.body_);
  ForFrame loop(parsed->do_[0], body, nullptr, 2);
  loop.Enter(StepDataValue::FromBytes("null"));

  FakeFrameContext context;
  context.SetForIn(StepDataValue::FromBytes("[1,2,3,4,5]"));

  auto result = loop.Advance(context);
  auto* failed = std::get_if<Failed>(&result);
  ASSERT_NE(failed, nullptr);
  EXPECT_EQ(failed->failure_.status_, task::TASK_STATUS_INTERNAL);
  EXPECT_EQ(failed->failure_.step_, "loop");
  EXPECT_NE(failed->failure_.message_.find("budget"), std::string::npos);
  EXPECT_EQ(context.Calls().size(), 2U);
}

TEST(ForFrameTest, ANestedDoInsideTheBodyInheritsTheLoopScope) {
  auto parsed = ::strij::openworkflow::Parse(kForNestedDo);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.SetForIn(StepDataValue::FromBytes(R"(["red"])"));
  context.ScriptInline("1");

  auto result = root->Advance(context);
  EXPECT_TRUE(std::holds_alternative<Completed>(result));
  EXPECT_EQ(context.ScopeItems().at("outer"), R"("red")");
  EXPECT_EQ(context.ScopeItems().at("a"), R"("red")");
}

TEST(SequenceFrameTest, AnAwaitedRunDispatchesAndSuspends) {
  auto parsed = ::strij::openworkflow::Parse(kAwaitedRun);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptSuspend("nested-1");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Suspended>(result));
  EXPECT_EQ(context.Runs(), std::vector<std::string>{"subflow"});
  EXPECT_TRUE(context.Calls().empty());
  ASSERT_TRUE(root->IsWaiting());
  EXPECT_EQ(root->WaitingChildId(), "nested-1");

  context.Deliver("nested-1", FakeFrameContext::Success(R"("nested-result")"));
  auto resumed = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Completed>(resumed));
  EXPECT_EQ(std::get<Completed>(resumed).value_.Bytes(), R"("nested-result")");
}

TEST(SequenceFrameTest, AnAwaitedRunCompletionAppliesTheStepPipeline) {
  auto parsed = ::strij::openworkflow::Parse(kAwaitedRun);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.ScriptInline("7");

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Completed>(result));
  EXPECT_EQ(context.Runs(), std::vector<std::string>{"subflow"});
  EXPECT_EQ(std::get<Completed>(result).value_.Bytes(), "7");
  ASSERT_EQ(context.Exports().size(), 1U);
  EXPECT_EQ(context.Exports()[0].step_, "subflow");
}

TEST(SequenceFrameTest, ADetachedRunCompletesWithItsTransformedInput) {
  auto parsed = ::strij::openworkflow::Parse(kDetachedRun);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;

  auto result = root->Advance(context);
  ASSERT_TRUE(std::holds_alternative<Completed>(result));
  EXPECT_EQ(context.Runs(), std::vector<std::string>{"subflow"});
  EXPECT_FALSE(root->IsWaiting());
  EXPECT_EQ(std::get<Completed>(result).value_.Bytes(), "null");
}

TEST(SequenceFrameTest, ARunDispatchFailureFailsTheStep) {
  auto parsed = ::strij::openworkflow::Parse(kAwaitedRun);
  ASSERT_TRUE(parsed.has_value());
  auto root = MakeRoot(*parsed);

  FakeFrameContext context;
  context.Script(FakeFrameContext::Effect{
      .kind_ = FakeFrameContext::EffectKind::kLocalError, .message_ = "cannot start"});

  auto result = root->Advance(context);
  auto* failed = std::get_if<Failed>(&result);
  ASSERT_NE(failed, nullptr);
  EXPECT_EQ(failed->failure_.status_, task::TASK_STATUS_INTERNAL);
  EXPECT_EQ(failed->failure_.step_, "subflow");
  EXPECT_NE(failed->failure_.message_.find("cannot start"), std::string::npos);
}

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
