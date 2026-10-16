#include "nodeagent/extensions/task_handlers/openworkflow/interpreter.hh"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/task/task.pb.h"
#include "gtest/gtest.h"
#include "openworkflow/document.hh"
#include "openworkflow/parser/parse.hh"
#include "test/nodeagent/extensions/task_handlers/openworkflow/fakes.hh"

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

class TestSink final : public ChildOutcomeSink {
public:
  void OnChildOutcome(const std::string& /*parent_id*/, const std::string& child_id,
                      ChildOutcome outcome) override {
    interpreter_->OnChildOutcome(child_id, std::move(outcome));
  }

  WorkflowInterpreter* interpreter_ = nullptr;
};

struct Harness {
  Harness() {
    interpreter_ = std::make_unique<WorkflowInterpreter>(
        submitter_, test::JqEvaluatorConfig(), sink_, "parent",
        [this](task::TaskResult result) { results_.push_back(std::move(result)); });
    sink_.interpreter_ = interpreter_.get();
  }

  test::FakeChildTaskSubmitter submitter_;
  TestSink sink_;
  std::vector<task::TaskResult> results_;
  std::unique_ptr<WorkflowInterpreter> interpreter_;
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
  harness.submitter_.SetOnSubmit(
      [](const task::Task& /*task*/, gateway::ResultReceiver& receiver) {
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
  const std::vector<const NamedTask*> steps = CollectSteps(document);
  ExpressionEngine engine(test::JqEvaluatorConfig());

  auto failure = ValidateDocument(document, steps, engine);
  ASSERT_TRUE(failure.has_value());
  EXPECT_EQ(failure->phase_, Phase::kValidation);
}

TEST(ValidateDocumentTest, DataDependentDocumentPassesValidation) {
  auto parsed = ::strij::openworkflow::Parse(kTwoStepsWithExpression);
  ASSERT_TRUE(parsed.has_value());
  const Document document = std::move(*parsed);
  const std::vector<const NamedTask*> steps = CollectSteps(document);
  ExpressionEngine engine(test::JqEvaluatorConfig());

  EXPECT_FALSE(ValidateDocument(document, steps, engine).has_value());
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
