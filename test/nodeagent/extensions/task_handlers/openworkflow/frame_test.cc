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

  void Script(Effect effect) { effects_.push_back(std::move(effect)); }

  void ScriptSuspend(std::string child_id) {
    Script(Effect{.kind_ = EffectKind::kSuspend, .child_id_ = std::move(child_id)});
  }

  void ScriptInline(std::string body) {
    Script(Effect{.kind_ = EffectKind::kInline, .body_ = std::move(body)});
  }

  auto EvaluateCondition(const NamedTask& /*step*/, const StepDataValue& /*raw*/)
      -> absl::StatusOr<bool> override {
    return true;
  }

  auto TransformTaskInput(const NamedTask& /*step*/, const StepDataValue& raw)
      -> absl::StatusOr<StepDataValue> override {
    return raw.Copy();
  }

  auto TransformTaskOutput(const NamedTask& /*step*/, const StepDataValue& /*input*/,
                           const StepDataValue& raw_output)
      -> absl::StatusOr<StepDataValue> override {
    return raw_output.Copy();
  }

  auto ExecuteCall(SequenceFrame& owner, const NamedTask& step, const StepDataValue& /*input*/)
      -> absl::Status override {
    calls_.push_back(step.name_);

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
  [[nodiscard]] auto Forgotten() const -> const std::vector<std::string>& { return forgotten_; }

  [[nodiscard]] static auto Success(std::string body) -> ChildOutcome {
    return ChildOutcome{.body_ = std::move(body), .reason_ = {}, .status_ = task::TASK_STATUS_OK};
  }

  [[nodiscard]] static auto Error(std::string reason, task::TaskStatus status) -> ChildOutcome {
    return ChildOutcome{.body_ = {}, .reason_ = std::move(reason), .status_ = status};
  }

private:
  std::deque<Effect> effects_;
  std::vector<std::string> calls_;
  std::vector<std::string> forgotten_;
  std::map<std::string, SequenceFrame*> waiting_;
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

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
