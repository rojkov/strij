#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "test/nodeagent/extensions/task_handlers/openworkflow/fakes.hh"

#include "common/task/task.pb.h"
#include "gtest/gtest.h"
#include "nodeagent/extensions/task_handlers/openworkflow/interpreter.hh"
#include "openworkflow/document.hh"
#include "openworkflow/parser/parse.hh"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

using ::strij::openworkflow::Document;
using ::strij::openworkflow::NamedTask;

// NOLINTBEGIN(modernize-use-trailing-return-type)

constexpr std::string_view kSingleStep = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: single
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kTwoSteps = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: two
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

constexpr std::string_view kThreeSteps = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: three
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
  - three:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kTwoStepsWithExpression = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: expr
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
        parameters:
          p: '${ .field }'
)YAML";

constexpr std::string_view kUnknownKey = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bad
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
        bogus: value
)YAML";

constexpr std::string_view kBadRequirement = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bad
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
        requirements:
          cpu: 1.5
)YAML";

constexpr std::string_view kPartialWrapper = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: partial
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
        parameters:
          p: 'jobs/${ .id }'
)YAML";

constexpr std::string_view kNestedMalformed = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: nested
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
        parameters:
          outer:
            inner: '${ .x( }'
)YAML";

constexpr std::string_view kReservedTarget = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: reserved
  version: '0.1.0'
do:
  - one:
      call: http
      with:
        method: get
)YAML";

constexpr std::string_view kUnknownTarget = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: unknown
  version: '0.1.0'
do:
  - one:
      call: bogus
      with:
        type: echo
)YAML";

constexpr std::string_view kDeclaredFunction = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: function
  version: '0.1.0'
use:
  functions:
    getPet:
      call: strij
      with:
        type: echo
do:
  - one:
      call: getPet
      with:
        type: echo
)YAML";

constexpr std::string_view kLooseMode = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: loose
  version: '0.1.0'
evaluate:
  mode: loose
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kSeedInput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: seed
  version: '0.1.0'
input:
  from: '${ {"greeting":"hello"} }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kIdentityStep = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: identity
  version: '0.1.0'
do:
  - one:
      call: strij
      input:
        from: ${ . }
      with:
        type: echo
)YAML";

constexpr std::string_view kDocumentInputFromBody = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: from-body
  version: '0.1.0'
input:
  from: '${ .input }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kDocumentInputIdentity = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: input-identity
  version: '0.1.0'
input:
  from: '${ . }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kNestedDo = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: nested-do
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
  - c:
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

constexpr std::string_view kForkTwoBranches = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-two
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

constexpr std::string_view kForkJoinAllThenStep = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-then-step
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
  - after:
      call: strij
      with:
        type: echo
        parameters:
          p: '${ .[1].b }'
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
  - after:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kForkNested = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-nested
  version: '0.1.0'
do:
  - outer:
      fork:
        branches:
          - nested:
              do:
                - inner:
                    fork:
                      compete: true
                      branches:
                        - x:
                            call: strij
                            with:
                              type: echo
                        - y:
                            call: strij
                            with:
                              type: echo
          - plain:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kForkCompeteAbandonsNested = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-compete-nested
  version: '0.1.0'
do:
  - race:
      fork:
        compete: true
        branches:
          - nested:
              do:
                - inner:
                    fork:
                      branches:
                        - x:
                            do:
                              - x1:
                                  call: strij
                                  with:
                                    type: echo
                              - x2:
                                  call: strij
                                  with:
                                    type: echo
                        - y:
                            do:
                              - y1:
                                  call: strij
                                  with:
                                    type: echo
                              - y2:
                                  call: strij
                                  with:
                                    type: echo
          - plain:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kForkMultiStepBranch = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-multi-step
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - chained:
              do:
                - first:
                    call: strij
                    with:
                      type: echo
                - second:
                    call: strij
                    input:
                      from: ${ . }
                    with:
                      type: echo
                      parameters:
                        p: '${ .value }'
          - plain:
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
          - chained:
              do:
                - inner:
                    call: strij
                    with:
                      type: echo
          - plain:
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
          - outer:
              fork:
                branches:
                  - leaf:
                      call: strij
                      with:
                        type: echo
)YAML";

constexpr std::string_view kBranchUnknownTarget = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: branch-unknown
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              call: bogus
              with:
                type: echo
          - b:
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

constexpr std::string_view kEmptyCompetingFork = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: empty-compete
  version: '0.1.0'
do:
  - fan:
      fork:
        compete: true
        branches: []
)YAML";

constexpr std::string_view kDocumentInputString = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: input-string
  version: '0.1.0'
input:
  from: '${ .name }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kPlainStep = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: plain
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kCallInputFromWithExpression = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: call-input
  version: '0.1.0'
do:
  - one:
      call: strij
      input:
        from: '${ .payload }'
      with:
        type: echo
        parameters:
          p: '${ .id }'
)YAML";

constexpr std::string_view kCallInputFromString = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: call-input-string
  version: '0.1.0'
do:
  - one:
      call: strij
      input:
        from: '${ .name }'
      with:
        type: echo
)YAML";

constexpr std::string_view kNestedDoInputFrom = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: nested-input
  version: '0.1.0'
do:
  - outer:
      input:
        from: '${ .payload }'
      do:
        - a:
            call: strij
            with:
              type: echo
              parameters:
                p: '${ .id }'
  - after:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kForkInputFrom = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-input
  version: '0.1.0'
do:
  - fan:
      input:
        from: '${ .payload }'
      fork:
        branches:
          - a:
              call: strij
              with:
                type: echo
                parameters:
                  p: '${ .id }'
          - b:
              call: strij
              with:
                type: echo
                parameters:
                  p: '${ .id }'
)YAML";

constexpr std::string_view kConditionFalseSkip = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: condition-false
  version: '0.1.0'
do:
  - maybe:
      if: '${ .enabled }'
      then: exit
      output:
        as: '${ 1 }'
      call: strij
      with:
        type: echo
        parameters:
          p: '${ .id }'
  - next:
      call: strij
      with:
        type: echo
        parameters:
          p: '${ .id }'
)YAML";

constexpr std::string_view kConditionTrue = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: condition-true
  version: '0.1.0'
do:
  - one:
      if: '${ .enabled }'
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kConditionNestedDoSkip = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: condition-nested
  version: '0.1.0'
do:
  - outer:
      do:
        - a:
            if: '${ .enabled }'
            call: strij
            with:
              type: echo
        - b:
            call: strij
            with:
              type: echo
)YAML";

constexpr std::string_view kConditionForkBranchSkip = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: condition-fork
  version: '0.1.0'
do:
  - fan:
      fork:
        branches:
          - a:
              if: '${ .enabled }'
              call: strij
              with:
                type: echo
          - b:
              call: strij
              with:
                type: echo
)YAML";

constexpr std::string_view kConditionNonBoolean = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: condition-number
  version: '0.1.0'
do:
  - one:
      if: '${ .count }'
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kConditionOpaqueSubject = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: condition-opaque
  version: '0.1.0'
do:
  - one:
      if: '${ .enabled }'
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kBareIf = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bare-if
  version: '0.1.0'
do:
  - one:
      if: .enabled
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kMalformedIf = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: malformed-if
  version: '0.1.0'
do:
  - one:
      if: '${ .enabled( }'
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kBareInputFrom = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bare-input
  version: '0.1.0'
do:
  - one:
      call: strij
      input:
        from: .payload
      with:
        type: echo
)YAML";

constexpr std::string_view kMalformedInputFrom = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: malformed-input
  version: '0.1.0'
do:
  - one:
      call: strij
      input:
        from: '${ .payload( }'
      with:
        type: echo
)YAML";

constexpr std::string_view kNestedBareInputFrom = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: nested-bare-input
  version: '0.1.0'
do:
  - one:
      call: strij
      input:
        from:
          id: .payload.id
      with:
        type: echo
)YAML";

constexpr std::string_view kBareOutputAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bare-output
  version: '0.1.0'
do:
  - one:
      call: strij
      output:
        as: .result
      with:
        type: echo
)YAML";

constexpr std::string_view kMalformedOutputAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: malformed-output
  version: '0.1.0'
do:
  - one:
      call: strij
      output:
        as: '${ .result( }'
      with:
        type: echo
)YAML";

constexpr std::string_view kBareDocumentOutput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: bare-document-output
  version: '0.1.0'
output:
  as: .result
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kMalformedDocumentOutput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: malformed-document-output
  version: '0.1.0'
output:
  as: '${ .result( }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kCallOutputAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: call-output
  version: '0.1.0'
do:
  - one:
      call: strij
      output:
        as: '${ .id }'
      with:
        type: echo
  - two:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kDoOutputAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: do-output
  version: '0.1.0'
do:
  - outer:
      output:
        as: '${ .id }'
      do:
        - inner:
            call: strij
            with:
              type: echo
  - after:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kForkOutputAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: fork-output
  version: '0.1.0'
do:
  - fan:
      output:
        as: '${ .[0].id }'
      fork:
        branches:
          - a:
              call: strij
              with:
                type: echo
  - after:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kOutputAsUsesInputAndOutput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: output-vars
  version: '0.1.0'
do:
  - one:
      call: strij
      input:
        from: '${ {seed: .seed} }'
      output:
        as: '${ {before: $input, after: $output} }'
      with:
        type: echo
  - two:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kOutputAsOpaqueOutput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: opaque-output
  version: '0.1.0'
do:
  - one:
      call: strij
      output:
        as: '${ . }'
      with:
        type: echo
)YAML";

constexpr std::string_view kOutputAsOpaqueInput = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: opaque-input
  version: '0.1.0'
do:
  - one:
      call: strij
      with:
        type: echo
  - two:
      call: strij
      output:
        as: '${ . }'
      with:
        type: echo
)YAML";

constexpr std::string_view kDocumentOutputAs = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: document-output
  version: '0.1.0'
output:
  as: '${ {petId: .id} }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

constexpr std::string_view kDocumentOutputOpaque = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: document-output-opaque
  version: '0.1.0'
output:
  as: '${ .id }'
do:
  - one:
      call: strij
      with:
        type: echo
)YAML";

struct Harness {
  Harness() {
    interpreter_ = std::make_shared<WorkflowInterpreter>(
        submitter_, test::JqEvaluatorConfig(), "parent",
        [this](task::TaskResult result) { results_.push_back(std::move(result)); });
  }

  test::FakeChildTaskSubmitter submitter_;
  std::vector<task::TaskResult> results_;
  std::shared_ptr<WorkflowInterpreter> interpreter_;
};

TEST(InterpreterTest, RunsThreeStepsInDeclarationOrder) {
  Harness harness;
  auto counter = std::make_shared<int>(0);
  harness.submitter_.SetOnSubmit(
      [counter](const task::Task& /*task*/, gateway::ResultReceiver& receiver) {
        const std::string body = std::to_string(++*counter);
        const auto data = std::as_bytes(std::span(body.data(), body.size()));
        receiver.Deliver(data, true);
      });

  harness.interpreter_->Start(std::string(kThreeSteps));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "3");
  EXPECT_TRUE(harness.results_[0].is_final());
  EXPECT_EQ(*counter, 3);
}

TEST(InterpreterTest, LocalChildCompletingInsideSubmitAdvancesExactlyOnce) {
  Harness harness;
  harness.submitter_.SetOnSubmit([](const task::Task& /*task*/, gateway::ResultReceiver& receiver) {
    const std::string body = "done";
    const auto data = std::as_bytes(std::span(body.data(), body.size()));
    receiver.Deliver(data, true);
  });

  harness.interpreter_->Start(std::string(kSingleStep));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "done");
  EXPECT_EQ(harness.submitter_.size(), 1U);
}

TEST(InterpreterTest, SuspendsAcrossChildResultAndResumesAtFollowingStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kTwoSteps));
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccess(harness.submitter_, "1");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccess(harness.submitter_, "2");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "2");
}

TEST(InterpreterTest, NestedDoRunsAtItsPositionAndResumesAfterIt) {
  Harness harness;
  harness.interpreter_->Start(std::string(kNestedDo));
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 0, "1");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 1, "2");
  ASSERT_EQ(harness.submitter_.size(), 3U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 2, "3");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "3");
}

TEST(InterpreterTest, IgnoresOutcomeForUnknownChildId) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSingleStep));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  ChildOutcome stale;
  stale.body_ = "stale";
  harness.interpreter_->OnChildOutcome("some-other-id", std::move(stale));

  EXPECT_TRUE(harness.results_.empty());
  EXPECT_EQ(harness.submitter_.size(), 1U);
}

TEST(InterpreterTest, ChildFailureAfterSuspensionReportsOnceAndStops) {
  Harness harness;
  harness.interpreter_->Start(std::string(kTwoSteps));
  test::CompleteError(harness.submitter_, "refused", task::TASK_STATUS_CAPACITY_REFUSED);

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_CAPACITY_REFUSED);
  EXPECT_NE(harness.results_[0].body().find("one"), std::string::npos);
  EXPECT_TRUE(harness.results_[0].is_final());
  EXPECT_EQ(harness.submitter_.size(), 1U);
}

TEST(InterpreterTest, SeedInputExpressionBecomesFirstChildBody) {
  Harness harness;
  harness.interpreter_->Start(std::string(kSeedInput));
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"greeting":"hello"})");
}

TEST(InterpreterTest, InvocationBodySeedsFirstChildBody) {
  Harness harness;
  harness.interpreter_->Start(std::string(kIdentityStep), R"({"input":5})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"input":5})");
}

TEST(InterpreterTest, DocumentInputTransformsInvocationBody) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDocumentInputFromBody), R"({"input":5})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), "5");
}

TEST(InterpreterTest, EmptyBodySeedsJsonNull) {
  Harness harness;
  harness.interpreter_->Start(std::string(kIdentityStep));
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), "null");
}

TEST(InterpreterTest, EmptyBodyIsTheDocumentInputSubject) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDocumentInputIdentity));
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), "null");
}

TEST(InterpreterTest, OpaqueBodyHasNoJsonSubject) {
  Harness harness;
  harness.interpreter_->Start(std::string(kIdentityStep), "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterTest, MarksWorkflowFailedForNonJsonSubjectDuringExecution) {
  Harness harness;
  harness.interpreter_->Start(std::string(kTwoStepsWithExpression));
  test::CompleteSuccess(harness.submitter_, "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("two"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 1U);
}

TEST(InterpreterValidationTest, UnknownKeyFailsBeforeAnySubmission) {
  Harness harness;
  harness.interpreter_->Start(std::string(kUnknownKey));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, BadRequirementFailsBeforeAnySubmission) {
  Harness harness;
  harness.interpreter_->Start(std::string(kBadRequirement));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, PartialWrapperFailsBeforeAnySubmission) {
  Harness harness;
  harness.interpreter_->Start(std::string(kPartialWrapper));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, MalformedNestedExpressionFailsBeforeAnySubmission) {
  Harness harness;
  harness.interpreter_->Start(std::string(kNestedMalformed));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, ReservedTargetIsUnimplemented) {
  Harness harness;
  harness.interpreter_->Start(std::string(kReservedTarget));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_UNIMPLEMENTED);
  EXPECT_NE(harness.results_[0].body().find("recognized"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, UnknownTargetIsMalformedRequest) {
  Harness harness;
  harness.interpreter_->Start(std::string(kUnknownTarget));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_NE(harness.results_[0].body().find("not recognized"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, DeclaredFunctionIsUnimplementedAndDistinct) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDeclaredFunction));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_UNIMPLEMENTED);
  EXPECT_NE(harness.results_[0].body().find("declared function"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, LooseModeIsRejected) {
  Harness harness;
  harness.interpreter_->Start(std::string(kLooseMode));
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_NE(harness.results_[0].body().find("loose"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(ValidateDocumentTest, ValidationFailureCarriesValidationPhase) {
  auto parsed = ::strij::openworkflow::Parse(kUnknownKey);
  ASSERT_TRUE(parsed.has_value());
  const Document document = std::move(*parsed);
  ExpressionEngine engine(test::JqEvaluatorConfig());

  auto failure = ValidateDocument(document, engine);
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->phase_, Phase::kValidation);
}

TEST(ValidateDocumentTest, DataDependentDocumentPassesValidation) {
  auto parsed = ::strij::openworkflow::Parse(kTwoStepsWithExpression);
  ASSERT_TRUE(parsed.has_value());
  const Document document = std::move(*parsed);
  ExpressionEngine engine(test::JqEvaluatorConfig());

  EXPECT_FALSE(ValidateDocument(document, engine).has_value());
}

TEST(InterpreterValidationTest, MalformedCallInsideForkBranchFailsBeforeAnySubmission) {
  Harness harness;
  harness.interpreter_->Start(std::string(kBranchUnknownTarget));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_NE(harness.results_[0].body().find("not recognized"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, CompetingForkWithoutBranchesIsMalformed) {
  Harness harness;
  harness.interpreter_->Start(std::string(kEmptyCompetingFork));

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_NE(harness.results_[0].body().find("at least one branch"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(ForkInterpreterTest, SubmitsEveryBranchBeforeResolving) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkThreeBranches));

  ASSERT_EQ(harness.submitter_.size(), 3U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 0, "1");
  test::CompleteSuccessAt(harness.submitter_, 1, "2");
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 2, "3");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "[1,2,3]");
}

TEST(ForkInterpreterTest, EmptyJoinAllForkJoinsToTheEmptyArray) {
  Harness harness;
  harness.interpreter_->Start(std::string(kEmptyJoinAllFork));

  ASSERT_EQ(harness.submitter_.size(), 0U);
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "[]");
  EXPECT_TRUE(harness.results_[0].is_final());
}

TEST(ForkInterpreterTest, JoinAllOrdersResultsByDeclarationNotCompletion) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkThreeBranches));
  ASSERT_EQ(harness.submitter_.size(), 3U);

  test::CompleteSuccessAt(harness.submitter_, 2, "3");
  test::CompleteSuccessAt(harness.submitter_, 0, "1");
  test::CompleteSuccessAt(harness.submitter_, 1, "2");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "[1,2,3]");
}

TEST(ForkInterpreterTest, JoinAllArrayIsTheFollowingStepSubject) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkJoinAllThenStep));
  ASSERT_EQ(harness.submitter_.size(), 2U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"a":1})");
  test::CompleteSuccessAt(harness.submitter_, 1, R"({"b":2})");

  ASSERT_EQ(harness.submitter_.size(), 3U);
  ASSERT_NE(harness.submitter_.at(2), nullptr);
  const auto& parameters = harness.submitter_.at(2)->task_.parameters();
  const auto parameter = parameters.find("p");
  ASSERT_NE(parameter, parameters.end());
  EXPECT_EQ(parameter->second, "2");
}

TEST(ForkInterpreterTest, JoinAllFailsOnAnOpaqueBranchResult) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkThreeBranches));
  ASSERT_EQ(harness.submitter_.size(), 3U);

  test::CompleteSuccessAt(harness.submitter_, 0, "not json");
  test::CompleteSuccessAt(harness.submitter_, 1, "2");
  test::CompleteSuccessAt(harness.submitter_, 2, "3");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("branch 'a'"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("not a JSON value"), std::string::npos);
}

TEST(ForkInterpreterTest, NestedDoFailureInsideABranchNamesTheInnerStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkNestedDoFailure));
  ASSERT_EQ(harness.submitter_.size(), 2U);

  test::CompleteErrorAt(harness.submitter_, 0, "boom", task::TASK_STATUS_CAPACITY_REFUSED);

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_CAPACITY_REFUSED);
  EXPECT_EQ(harness.results_[0].body(), "branch 'chained': step 'inner': boom");

  test::CompleteSuccessAt(harness.submitter_, 1, "2");
  EXPECT_EQ(harness.results_.size(), 1U);
}

TEST(ForkInterpreterTest, NestedJoinFailureIsNamedByTheInnerBranchOnly) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkNestedJoinFailure));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_EQ(harness.results_[0].body(), "branch 'leaf': the branch result is not a JSON value");
}

TEST(ForkInterpreterTest, CompeteResolvesWithTheFirstTerminalResult) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkCompete));
  ASSERT_EQ(harness.submitter_.size(), 2U);

  test::CompleteSuccessAt(harness.submitter_, 1, "7");

  ASSERT_EQ(harness.submitter_.size(), 3U);
  ASSERT_NE(harness.submitter_.at(2), nullptr);
  EXPECT_EQ(harness.submitter_.at(2)->task_.body(), "7");
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 0, "1");
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 2, "9");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "9");
}

TEST(ForkInterpreterTest, LateOutcomeAfterTheCompeteWinnerChangesNothing) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkCompete));
  ASSERT_EQ(harness.submitter_.size(), 2U);

  test::CompleteSuccessAt(harness.submitter_, 1, "7");
  EXPECT_TRUE(harness.results_.empty());

  // The losing branch's outcome arrives after the fork resolved. It must not
  // re-enter the workflow or change the join value.
  test::CompleteSuccessAt(harness.submitter_, 0, "1");
  EXPECT_TRUE(harness.results_.empty());
  EXPECT_EQ(harness.submitter_.size(), 3U);

  test::CompleteSuccessAt(harness.submitter_, 2, "9");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "9");
}

TEST(ForkInterpreterTest, JoinAllFailsFastWhenABranchFails) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkThreeBranches));
  ASSERT_EQ(harness.submitter_.size(), 3U);

  test::CompleteErrorAt(harness.submitter_, 0, "boom", task::TASK_STATUS_CAPACITY_REFUSED);

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_CAPACITY_REFUSED);
  EXPECT_NE(harness.results_[0].body().find("branch 'a'"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("boom"), std::string::npos);

  test::CompleteSuccessAt(harness.submitter_, 1, "2");
  test::CompleteSuccessAt(harness.submitter_, 2, "3");
  EXPECT_EQ(harness.results_.size(), 1U);
}

TEST(ForkInterpreterTest, CompeteCanBeDecidedByTheFirstFailure) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkCompete));
  ASSERT_EQ(harness.submitter_.size(), 2U);

  test::CompleteErrorAt(harness.submitter_, 0, "boom", task::TASK_STATUS_UNAVAILABLE);

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_UNAVAILABLE);
  EXPECT_NE(harness.results_[0].body().find("branch 'fast'"), std::string::npos);

  test::CompleteSuccessAt(harness.submitter_, 1, "2");
  EXPECT_EQ(harness.results_.size(), 1U);
}

TEST(ForkInterpreterTest, InlineBranchCompletionDoesNotDisturbItsSibling) {
  Harness harness;
  auto inline_count = std::make_shared<int>(0);
  harness.submitter_.SetOnSubmit(
      [inline_count](const task::Task& /*task*/, gateway::ResultReceiver& receiver) {
        if (++*inline_count != 1) {
          return;
        }

        const std::string body = "1";
        const auto data = std::as_bytes(std::span(body.data(), body.size()));
        receiver.Deliver(data, true);
      });

  harness.interpreter_->Start(std::string(kForkTwoBranches));

  // Branch a completed inline; branch b was still submitted and awaits.
  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 1, "2");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "[1,2]");
}

TEST(ForkInterpreterTest, MultiStepBranchKeepsItsOwnChain) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkMultiStepBranch));
  ASSERT_EQ(harness.submitter_.size(), 2U);

  // The sibling's result must not become the chained branch's subject.
  test::CompleteSuccessAt(harness.submitter_, 1, R"({"value":"other"})");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"value":"one"})");
  ASSERT_EQ(harness.submitter_.size(), 3U);
  ASSERT_NE(harness.submitter_.at(2), nullptr);
  EXPECT_EQ(harness.submitter_.at(2)->task_.body(), R"({"value":"one"})");

  const auto& parameters = harness.submitter_.at(2)->task_.parameters();
  const auto parameter = parameters.find("p");
  ASSERT_NE(parameter, parameters.end());
  EXPECT_EQ(parameter->second, "one");

  test::CompleteSuccessAt(harness.submitter_, 2, R"({"value":"two"})");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"([{"value":"two"},{"value":"other"}])");
}

TEST(ForkInterpreterTest, CompeteAbandonsOutstandingGrandchildren) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkCompeteAbandonsNested));

  // x1, y1, and the plain branch are submitted; the nested branch is waiting on
  // the inner fork.
  ASSERT_EQ(harness.submitter_.size(), 3U);

  test::CompleteSuccessAt(harness.submitter_, 2, "1");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "1");

  // The inner branch's first child completes after the outer fork resolved. It
  // must not advance that abandoned branch into its second submission.
  test::CompleteSuccessAt(harness.submitter_, 0, "10");
  EXPECT_EQ(harness.submitter_.size(), 3U);
  EXPECT_EQ(harness.results_.size(), 1U);
}

TEST(ForkInterpreterTest, NestedForkJoinBecomesTheBranchResult) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkNested));

  // The nested compete fork submits x and y before the sibling branch.
  ASSERT_EQ(harness.submitter_.size(), 3U);

  test::CompleteSuccessAt(harness.submitter_, 0, "10");
  EXPECT_TRUE(harness.results_.empty());

  // y lost the inner compete and is abandoned.
  test::CompleteSuccessAt(harness.submitter_, 1, "20");
  EXPECT_TRUE(harness.results_.empty());

  test::CompleteSuccessAt(harness.submitter_, 2, "30");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "[10,30]");
}

TEST(InterpreterTest, DocumentInputPreservesAJsonString) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDocumentInputString), R"({"name":"world"})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"("world")");
}

TEST(InterpreterTest, OpaqueInputPassesThroughWhenNoTransformationIsDeclared) {
  Harness harness;
  harness.interpreter_->Start(std::string(kPlainStep), "not json");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), "not json");
}

TEST(InterpreterInputFromTest, TransformedInputIsTheCallArgumentSubjectAndBody) {
  Harness harness;
  harness.interpreter_->Start(std::string(kCallInputFromWithExpression),
                              R"({"payload":{"id":7},"id":99})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"id":7})");
  EXPECT_EQ(harness.submitter_.last()->task_.parameters().at("p"), "7");
}

TEST(InterpreterInputFromTest, TransformedInputPreservesAJsonString) {
  Harness harness;
  harness.interpreter_->Start(std::string(kCallInputFromString), R"({"name":"world"})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"("world")");
}

TEST(InterpreterInputFromTest, TransformedInputSeedsANestedDo) {
  Harness harness;
  harness.interpreter_->Start(std::string(kNestedDoInputFrom), R"({"payload":{"id":5},"id":1})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"id":5})");
  EXPECT_EQ(harness.submitter_.last()->task_.parameters().at("p"), "5");

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"id":6})");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(), R"({"id":6})");
}

TEST(InterpreterInputFromTest, TransformedInputSeedsEveryForkBranch) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkInputFrom), R"({"payload":{"id":5},"id":1})");
  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(0)->task_.body(), R"({"id":5})");
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(), R"({"id":5})");
  EXPECT_EQ(harness.submitter_.at(0)->task_.parameters().at("p"), "5");
  EXPECT_EQ(harness.submitter_.at(1)->task_.parameters().at("p"), "5");
}

TEST(InterpreterConditionTest, TrueConditionRunsTheTask) {
  Harness harness;
  harness.interpreter_->Start(std::string(kConditionTrue), R"({"enabled":true})");
  EXPECT_EQ(harness.submitter_.size(), 1U);
}

TEST(InterpreterConditionTest, FalseConditionSkipsTheWholePipeline) {
  Harness harness;
  harness.interpreter_->Start(std::string(kConditionFalseSkip), R"({"enabled":false,"id":5})");

  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"enabled":false,"id":5})");
  EXPECT_EQ(harness.submitter_.last()->task_.parameters().at("p"), "5");
}

TEST(InterpreterConditionTest, FalseConditionSkipsANestedStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kConditionNestedDoSkip), R"({"enabled":false})");

  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"enabled":false})");
}

TEST(InterpreterConditionTest, FalseConditionSkipsAForkBranch) {
  Harness harness;
  harness.interpreter_->Start(std::string(kConditionForkBranchSkip), R"({"enabled":false})");
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"b":2})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"([{"enabled":false},{"b":2}])");
}

TEST(InterpreterConditionTest, NonBooleanConditionFailsTheStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kConditionNonBoolean), R"({"count":3})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("one"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("boolean"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterConditionTest, ConditionOverOpaqueInputFailsTheStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kConditionOpaqueSubject), "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("one"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
  EXPECT_EQ(harness.submitter_.size(), 0U);
}

TEST(InterpreterValidationTest, BareAndMalformedConditionsFailBeforeSubmission) {
  for (std::string_view document : {kBareIf, kMalformedIf}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

TEST(InterpreterValidationTest, BareAndMalformedInputFromFailBeforeSubmission) {
  for (std::string_view document : {kBareInputFrom, kMalformedInputFrom, kNestedBareInputFrom}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

TEST(InterpreterValidationTest, BareAndMalformedOutputAsFailBeforeSubmission) {
  for (std::string_view document : {kBareOutputAs, kMalformedOutputAs}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

TEST(InterpreterValidationTest, BareAndMalformedDocumentOutputFailBeforeSubmission) {
  for (std::string_view document : {kBareDocumentOutput, kMalformedDocumentOutput}) {
    Harness harness;
    harness.interpreter_->Start(std::string(document));

    ASSERT_EQ(harness.results_.size(), 1U);
    EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
    EXPECT_EQ(harness.submitter_.size(), 0U);
  }
}

TEST(InterpreterOutputAsTest, ResumedCallOutputIsShapedOnceForTheFollowingStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kCallOutputAs));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"id":7})");

  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(), "7");
}

TEST(InterpreterOutputAsTest, InlineCallOutputIsShapedExactlyOnce) {
  Harness harness;
  auto inline_count = std::make_shared<int>(0);
  harness.submitter_.SetOnSubmit(
      [inline_count](const task::Task& /*task*/, gateway::ResultReceiver& receiver) {
        if (++*inline_count != 1) {
          return;
        }

        const std::string body = R"({"id":7})";
        const auto data = std::as_bytes(std::span(body.data(), body.size()));
        receiver.Deliver(data, true);
      });

  harness.interpreter_->Start(std::string(kCallOutputAs));

  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(), "7");
}

TEST(InterpreterOutputAsTest, NestedSequenceOutputIsShaped) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDoOutputAs));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"id":7})");

  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(), "7");
}

TEST(InterpreterOutputAsTest, ForkJoinOutputIsShaped) {
  Harness harness;
  harness.interpreter_->Start(std::string(kForkOutputAs));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"id":7})");

  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(), "7");
}

TEST(InterpreterOutputAsTest, OutputExpressionBindsInputAndOutput) {
  Harness harness;
  harness.interpreter_->Start(std::string(kOutputAsUsesInputAndOutput), R"({"seed":1})");
  ASSERT_EQ(harness.submitter_.size(), 1U);
  EXPECT_EQ(harness.submitter_.last()->task_.body(), R"({"seed":1})");

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"name":"pet"})");

  ASSERT_EQ(harness.submitter_.size(), 2U);
  EXPECT_EQ(harness.submitter_.at(1)->task_.body(),
            R"({"after":{"name":"pet"},"before":{"seed":1}})");
}

TEST(InterpreterOutputAsTest, OpaqueRawOutputFailsTheStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kOutputAsOpaqueOutput), R"({"id":1})");
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("one"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
}

TEST(InterpreterOutputAsTest, OpaqueTransformedInputFailsTheStep) {
  Harness harness;
  harness.interpreter_->Start(std::string(kOutputAsOpaqueInput));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, "not json");
  ASSERT_EQ(harness.submitter_.size(), 2U);

  test::CompleteSuccessAt(harness.submitter_, 1, R"({"id":7})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("two"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("input"), std::string::npos);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
}

TEST(InterpreterDocumentOutputTest, ShapesTheFinalResultExactlyOnce) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDocumentOutputAs));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, R"({"id":7})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"petId":7})");
  EXPECT_TRUE(harness.results_[0].is_final());
}

TEST(InterpreterDocumentOutputTest, OpaqueFinalValueFailsTheRun) {
  Harness harness;
  harness.interpreter_->Start(std::string(kDocumentOutputOpaque));
  ASSERT_EQ(harness.submitter_.size(), 1U);

  test::CompleteSuccessAt(harness.submitter_, 0, "not json");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(harness.results_[0].body().find("not JSON"), std::string::npos);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
