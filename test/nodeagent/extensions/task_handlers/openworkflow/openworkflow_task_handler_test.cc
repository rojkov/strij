#include "nodeagent/extensions/task_handlers/openworkflow/openworkflow_task_handler.hh"

#include <memory>
#include <string>
#include <utility>

#include "common/task/task.pb.h"
#include "gtest/gtest.h"
#include "nodeagent/extensions/task_handlers/openworkflow/config.pb.h"
#include "strij/extensions/extension_registry.hh"
#include "test/nodeagent/extensions/task_handlers/openworkflow/fakes.hh"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

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
        type: piped_executable
        parameters:
          function: /bin/cat
        requirements:
          cpu: 2
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

auto MakeTask(const std::string& id, std::string body) -> task::Task {
  task::Task task;
  task.set_id(id);
  task.set_type("openworkflow");
  task.set_body(std::move(body));
  return task;
}

TEST(OpenWorkflowTaskHandlerFactoryTest, NameIsOpenworkflow) {
  OpenWorkflowTaskHandlerFactory factory;
  EXPECT_EQ(factory.Name(), "openworkflow");
}

TEST(OpenWorkflowTaskHandlerFactoryTest, IsRegistered) {
  auto& registry = extensions::Registry<nodeagent::TaskHandlerFactory>::instance();
  auto* factory = registry.GetFactory("openworkflow");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->Name(), "openworkflow");
}

TEST(OpenWorkflowTaskHandlerFactoryTest, ParseConfigReturnsCapacity) {
  extensions::task_handlers::openworkflow::OpenWorkflowTaskHandlerConfig config;
  config.mutable_capacity()->set_concurrency(7);
  OpenWorkflowTaskHandlerFactory factory;
  auto capacity = factory.ParseConfig(config);
  ASSERT_TRUE(capacity.ok());
  EXPECT_EQ(capacity->concurrency(), 7U);
}

TEST(OpenWorkflowTaskHandlerTest, ProjectsDeclaredTypeParametersAndRequirements) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  handler.HandleTask(MakeTask("wf", std::string(kSingleStep)),
                     std::make_unique<test::TestSender>(state));

  ASSERT_EQ(submitter.size(), 1U);
  const task::Task& child = submitter.last()->task_;
  EXPECT_EQ(child.type(), "piped_executable");
  ASSERT_EQ(child.parameters().count("function"), 1U);
  EXPECT_EQ(child.parameters().at("function"), "/bin/cat");
  EXPECT_EQ(child.requirements().resources().at("cpu"), 2U);

  test::CompleteSuccess(submitter, "child-output");
  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].id(), "wf");
  EXPECT_EQ(state->sent_[0].body(), "child-output");
  EXPECT_TRUE(state->sent_[0].is_final());
}

TEST(OpenWorkflowTaskHandlerTest, MalformedBodyIsMalformedRequest) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  handler.HandleTask(MakeTask("wf", "just: a scalar, no document key"),
                     std::make_unique<test::TestSender>(state));

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_EQ(submitter.size(), 0U);
}

TEST(OpenWorkflowTaskHandlerTest, ValidationFailureIsMalformedRequestWithoutChild) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  handler.HandleTask(MakeTask("wf", std::string(kUnknownKey)),
                     std::make_unique<test::TestSender>(state));

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_EQ(submitter.size(), 0U);
}

TEST(OpenWorkflowTaskHandlerTest, ReservedTargetIsUnimplemented) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  handler.HandleTask(MakeTask("wf", std::string(kReservedTarget)),
                     std::make_unique<test::TestSender>(state));

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(submitter.size(), 0U);
}

TEST(OpenWorkflowTaskHandlerTest, DataDependentFailureIsInternal) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  handler.HandleTask(MakeTask("wf", std::string(kTwoStepsWithExpression)),
                     std::make_unique<test::TestSender>(state));
  test::CompleteSuccess(submitter, "not json");

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(state->sent_[0].body().find("two"), std::string::npos);
}

TEST(OpenWorkflowTaskHandlerTest, PropagatesChildFailureStatus) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  handler.HandleTask(MakeTask("wf", std::string(kSingleStep)),
                     std::make_unique<test::TestSender>(state));
  test::CompleteError(submitter, "no such function", task::TASK_STATUS_NOT_FOUND);

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_NOT_FOUND);
  EXPECT_NE(state->sent_[0].body().find("one"), std::string::npos);
}

TEST(OpenWorkflowTaskHandlerTest, ConnectionTeardownMidSuspensionAbandonsRun) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  handler.HandleTask(MakeTask("wf", std::string(kSingleStep)),
                     std::make_unique<test::TestSender>(state));
  ASSERT_EQ(submitter.size(), 1U);
  EXPECT_TRUE(state->sent_.empty());

  ASSERT_TRUE(static_cast<bool>(state->close_cb_));
  state->close_cb_();

  // A late child outcome must be dropped and must not resume the workflow.
  test::CompleteSuccess(submitter, "late");
  EXPECT_TRUE(state->sent_.empty());
  EXPECT_EQ(submitter.size(), 1U);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
