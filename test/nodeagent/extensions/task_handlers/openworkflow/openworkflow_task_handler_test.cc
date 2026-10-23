#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "test/nodeagent/extensions/task_handlers/openworkflow/fakes.hh"

#include "common/task/task.pb.h"
#include "gtest/gtest.h"
#include "nodeagent/core/function_resolver.hh"
#include "nodeagent/extensions/task_handlers/openworkflow/config.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/openworkflow_task_handler.hh"
#include "strij/extensions/extension_registry.hh"

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

constexpr std::string_view kPassThroughStep = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: pass-through
  version: '0.1.0'
do:
  - one:
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

class OpenWorkflowTaskHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    static std::uint64_t counter = 0;
    dir_ = std::filesystem::temp_directory_path() /
           ("strij-openworkflow-handler-" + std::to_string(::getpid()) + "-" +
            std::to_string(counter++));
    std::filesystem::create_directories(dir_);
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }

  auto WriteArtifact(std::string_view name, std::string_view content) -> std::string {
    const std::filesystem::path path = dir_ / name;
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();

    return path.string();
  }

  static auto MakeTask(const std::string& task_id, const std::string& function, std::string body)
      -> task::Task {
    task::Task task;
    task.set_id(task_id);
    task.set_type("openworkflow");
    (*task.mutable_parameters())[std::string(kFunctionParameter)] = function;
    task.set_body(std::move(body));

    return task;
  }

  static auto MakeTask(const std::string& task_id, const std::string& function) -> task::Task {
    return MakeTask(task_id, function, {});
  }

  static auto MakeTaskWithoutFunction(const std::string& task_id, std::string body) -> task::Task {
    task::Task task;
    task.set_id(task_id);
    task.set_type("openworkflow");
    task.set_body(std::move(body));

    return task;
  }

  std::filesystem::path dir_;
  LocalFunctionResolver resolver_;
};

TEST_F(OpenWorkflowTaskHandlerTest, ProjectsDeclaredTypeParametersAndRequirements) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();
  const std::string document = WriteArtifact("single.yaml", kSingleStep);

  handler.HandleTask(MakeTask("wf", document), std::make_unique<test::TestSender>(state));

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

TEST_F(OpenWorkflowTaskHandlerTest, BodyIsPassedAsInvocationInput) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();
  const std::string document = WriteArtifact("identity.yaml", kIdentityStep);
  const std::string body = R"({"input":5})";

  handler.HandleTask(MakeTask("wf", document, body), std::make_unique<test::TestSender>(state));

  ASSERT_EQ(submitter.size(), 1U);
  EXPECT_EQ(submitter.last()->task_.body(), body);
}

TEST_F(OpenWorkflowTaskHandlerTest, DocumentShapedBodyIsNotParsedAsDocument) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();
  const std::string document = WriteArtifact("pass-through.yaml", kPassThroughStep);
  const std::string body(kReservedTarget);

  handler.HandleTask(MakeTask("wf", document, body), std::make_unique<test::TestSender>(state));

  ASSERT_EQ(submitter.size(), 1U);
  EXPECT_EQ(submitter.last()->task_.type(), "echo");
  EXPECT_EQ(submitter.last()->task_.body(), body);
}

TEST_F(OpenWorkflowTaskHandlerTest, MalformedDocumentIsMalformedRequest) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();
  const std::string document = WriteArtifact("malformed.yaml", "just: a scalar, no document key");

  handler.HandleTask(MakeTask("wf", document), std::make_unique<test::TestSender>(state));

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_TRUE(state->sent_[0].is_final());
  EXPECT_EQ(submitter.size(), 0U);
}

TEST_F(OpenWorkflowTaskHandlerTest, ValidationFailureIsMalformedRequestWithoutChild) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();
  const std::string document = WriteArtifact("unknown-key.yaml", kUnknownKey);

  handler.HandleTask(MakeTask("wf", document), std::make_unique<test::TestSender>(state));

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_EQ(submitter.size(), 0U);
}

TEST_F(OpenWorkflowTaskHandlerTest, ReservedTargetIsUnimplemented) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();
  const std::string document = WriteArtifact("reserved.yaml", kReservedTarget);

  handler.HandleTask(MakeTask("wf", document), std::make_unique<test::TestSender>(state));

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(submitter.size(), 0U);
}

TEST_F(OpenWorkflowTaskHandlerTest, DataDependentFailureIsInternal) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();
  const std::string document = WriteArtifact("expression.yaml", kTwoStepsWithExpression);

  handler.HandleTask(MakeTask("wf", document), std::make_unique<test::TestSender>(state));
  test::CompleteSuccess(submitter, "not json");

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_INTERNAL);
  EXPECT_NE(state->sent_[0].body().find("two"), std::string::npos);
}

TEST_F(OpenWorkflowTaskHandlerTest, PropagatesChildFailureStatus) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();
  const std::string document = WriteArtifact("single.yaml", kSingleStep);

  handler.HandleTask(MakeTask("wf", document), std::make_unique<test::TestSender>(state));
  test::CompleteError(submitter, "no such function", task::TASK_STATUS_NOT_FOUND);

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_NOT_FOUND);
  EXPECT_NE(state->sent_[0].body().find("one"), std::string::npos);
}

TEST_F(OpenWorkflowTaskHandlerTest, ConnectionTeardownMidSuspensionAbandonsRun) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();
  const std::string document = WriteArtifact("single.yaml", kSingleStep);

  handler.HandleTask(MakeTask("wf", document), std::make_unique<test::TestSender>(state));
  ASSERT_EQ(submitter.size(), 1U);
  EXPECT_TRUE(state->sent_.empty());

  ASSERT_TRUE(static_cast<bool>(state->close_cb_));
  state->close_cb_();

  // A late child outcome must be dropped and must not resume the workflow.
  test::CompleteSuccess(submitter, "late");
  EXPECT_TRUE(state->sent_.empty());
  EXPECT_EQ(submitter.size(), 1U);
}

TEST_F(OpenWorkflowTaskHandlerTest, MissingFunctionIsMalformedRequest) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  handler.HandleTask(MakeTaskWithoutFunction("wf", std::string(kSingleStep)),
                     std::make_unique<test::TestSender>(state));

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_TRUE(state->sent_[0].is_final());
  EXPECT_EQ(submitter.size(), 0U);
}

TEST_F(OpenWorkflowTaskHandlerTest, ResolverRejectionIsMalformedRequest) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  handler.HandleTask(MakeTask("wf", ""), std::make_unique<test::TestSender>(state));

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_TRUE(state->sent_[0].is_final());
  EXPECT_EQ(submitter.size(), 0U);
}

TEST_F(OpenWorkflowTaskHandlerTest, MissingArtifactIsNotFound) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  handler.HandleTask(MakeTask("wf", (dir_ / "missing.yaml").string()),
                     std::make_unique<test::TestSender>(state));

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_NOT_FOUND);
  EXPECT_TRUE(state->sent_[0].is_final());
  EXPECT_EQ(submitter.size(), 0U);
}

TEST_F(OpenWorkflowTaskHandlerTest, NonRegularArtifactIsMalformedRequest) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();

  handler.HandleTask(MakeTask("wf", dir_.string()), std::make_unique<test::TestSender>(state));

  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_TRUE(state->sent_[0].is_final());
  EXPECT_EQ(submitter.size(), 0U);
}

// ---------------------------------------------------------------------------
// Nested workflow runner
// ---------------------------------------------------------------------------

constexpr std::string_view kRunnerChild = R"YAML(document:
  dsl: '1.0.3'
  namespace: child
  name: child
  version: '0.1.0'
do:
  - inner-step:
      call: strij
      with:
        type: inner
)YAML";

constexpr std::string_view kRunnerRecursive = R"YAML(document:
  dsl: '1.0.3'
  namespace: outer
  name: outer
  version: '0.1.0'
do:
  - subflow:
      run:
        workflow:
          namespace: embedded
          name: middle
          version: '0.1.0'
          input: |
            document:
              dsl: '1.0.3'
              namespace: middle
              name: middle
              version: '0.1.0'
            do:
              - nested:
                  run:
                    workflow:
                      namespace: embedded
                      name: leaf
                      version: '0.1.0'
                      input: |
                        document:
                          dsl: '1.0.3'
                          namespace: leaf
                          name: leaf
                          version: '0.1.0'
                        do:
                          - leaf-step:
                              call: strij
                              with:
                                type: leaf
)YAML";

TEST(NestedWorkflowRunnerTest, StartsAndCompletesAwaitedRuns) {
  test::FakeChildTaskSubmitter submitter;
  NestedWorkflowRunner runner(submitter, test::JqEvaluatorConfig());
  std::vector<task::TaskResult> results;

  auto status = runner.Start(std::string(kRunnerChild), R"({"v":1})", "waited", 1,
                             [&results](task::TaskResult result) {
                               results.push_back(std::move(result));
                             });

  ASSERT_TRUE(status.ok());
  EXPECT_EQ(runner.Size(), 1U);
  ASSERT_EQ(submitter.size(), 1U);
  EXPECT_EQ(submitter.last()->task_.type(), "inner");

  test::CompleteSuccess(submitter, R"("done")");

  ASSERT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].body(), R"("done")");
  EXPECT_EQ(runner.Size(), 0U);
}

TEST(NestedWorkflowRunnerTest, DetachedRunsAreOwnedUntilCompletion) {
  test::FakeChildTaskSubmitter submitter;
  NestedWorkflowRunner runner(submitter, test::JqEvaluatorConfig());

  auto status = runner.Start(std::string(kRunnerChild), R"({"v":1})", "", 1, {});

  ASSERT_TRUE(status.ok());
  EXPECT_EQ(runner.Size(), 1U);
  ASSERT_EQ(submitter.size(), 1U);

  test::CompleteSuccess(submitter, R"("done")");
  EXPECT_EQ(runner.Size(), 0U);
}

TEST(NestedWorkflowRunnerTest, CancellingAnAwaitedRunStopsItsCompletion) {
  test::FakeChildTaskSubmitter submitter;
  NestedWorkflowRunner runner(submitter, test::JqEvaluatorConfig());
  bool completed = false;

  auto status = runner.Start(std::string(kRunnerChild), R"({"v":1})", "waited", 1,
                             [&completed](task::TaskResult /*result*/) { completed = true; });
  ASSERT_TRUE(status.ok());
  ASSERT_EQ(runner.Size(), 1U);

  runner.Cancel("waited");
  EXPECT_EQ(runner.Size(), 0U);

  test::CompleteSuccess(submitter, R"("late")");
  EXPECT_FALSE(completed);
}

TEST(NestedWorkflowRunnerTest, SupportsRecursiveEmbedding) {
  test::FakeChildTaskSubmitter submitter;
  NestedWorkflowRunner runner(submitter, test::JqEvaluatorConfig());
  std::vector<task::TaskResult> results;

  auto status = runner.Start(std::string(kRunnerRecursive), R"({"v":1})", "outer", 1,
                             [&results](task::TaskResult result) {
                               results.push_back(std::move(result));
                             });

  ASSERT_TRUE(status.ok());
  ASSERT_EQ(submitter.size(), 1U);
  EXPECT_EQ(submitter.last()->task_.type(), "leaf");

  test::CompleteSuccess(submitter, R"("leaf-result")");

  ASSERT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].body(), R"("leaf-result")");
  EXPECT_EQ(runner.Size(), 0U);
}

TEST(NestedWorkflowRunnerTest, RejectsRunsBeyondTheConfiguredDepth) {
  test::FakeChildTaskSubmitter submitter;
  NestedWorkflowRunner runner(submitter, test::JqEvaluatorConfig(), 1);

  auto status = runner.Start(std::string(kRunnerChild), {}, "too-deep", 2, {});

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(runner.Size(), 0U);
}

constexpr std::string_view kEmbeddedHandlerParent = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: embedded-handler
  version: '0.1.0'
do:
  - subflow:
      run:
        workflow:
          namespace: embedded
          name: child
          version: '0.1.0'
          input: |
            document:
              dsl: '1.0.3'
              namespace: child
              name: child
              version: '0.1.0'
            do:
              - cat-step:
                  call: strij
                  with:
                    type: piped_executable
                    parameters:
                      function: /bin/cat
                    requirements:
                      cpu: 1
)YAML";

constexpr std::string_view kEmbeddedDetachedParent = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: embedded-detached
  version: '0.1.0'
do:
  - subflow:
      run:
        await: false
        workflow:
          namespace: embedded
          name: child
          version: '0.1.0'
          input: |
            document:
              dsl: '1.0.3'
              namespace: child
              name: child
              version: '0.1.0'
            do:
              - cat-step:
                  call: strij
                  with:
                    type: piped_executable
                    parameters:
                      function: /bin/cat
                    requirements:
                      cpu: 1
)YAML";

TEST_F(OpenWorkflowTaskHandlerTest, ExecutesOneEmbeddedSubflow) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();
  const std::string document = WriteArtifact("embedded.yaml", kEmbeddedHandlerParent);

  handler.HandleTask(MakeTask("wf", document, R"({"v":1})"),
                     std::make_unique<test::TestSender>(state));

  ASSERT_EQ(submitter.size(), 1U);
  const task::Task& child = submitter.last()->task_;
  EXPECT_EQ(child.type(), "piped_executable");
  EXPECT_EQ(child.parameters().at("function"), "/bin/cat");
  EXPECT_EQ(child.body(), R"({"v":1})");
  EXPECT_TRUE(state->sent_.empty());

  test::CompleteSuccess(submitter, R"("child-output")");
  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].id(), "wf");
  EXPECT_EQ(state->sent_[0].body(), R"("child-output")");
}

TEST_F(OpenWorkflowTaskHandlerTest, ConnectionTeardownCancelsAnAwaitedSubflow) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();
  const std::string document = WriteArtifact("embedded.yaml", kEmbeddedHandlerParent);

  handler.HandleTask(MakeTask("wf", document, R"({"v":1})"),
                     std::make_unique<test::TestSender>(state));
  ASSERT_EQ(submitter.size(), 1U);
  ASSERT_TRUE(static_cast<bool>(state->close_cb_));

  state->close_cb_();
  test::CompleteSuccess(submitter, R"("late")");
  EXPECT_TRUE(state->sent_.empty());
}

TEST_F(OpenWorkflowTaskHandlerTest, ADetachedSubflowOutlivesItsParentRun) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();
  const std::string document = WriteArtifact("detached.yaml", kEmbeddedDetachedParent);

  handler.HandleTask(MakeTask("wf", document, R"({"v":1})"),
                     std::make_unique<test::TestSender>(state));

  // The parent does not await: it completes with its invocation input while the
  // detached child is still outstanding.
  ASSERT_EQ(state->sent_.size(), 1U);
  EXPECT_EQ(state->sent_[0].body(), R"({"v":1})");
  ASSERT_EQ(submitter.size(), 1U);

  // The parent run was reaped; the detached child still completes into the
  // runner and produces no parent result.
  test::CompleteSuccess(submitter, R"("detached")");
  EXPECT_EQ(state->sent_.size(), 1U);

  // The runner remains usable for the next run.
  auto second = std::make_shared<test::TestSender::State>();
  handler.HandleTask(MakeTask("wf2", document, R"({"v":2})"),
                     std::make_unique<test::TestSender>(second));
  ASSERT_EQ(second->sent_.size(), 1U);
  EXPECT_EQ(second->sent_[0].body(), R"({"v":2})");
}

constexpr std::string_view kEmbeddedDetachedWithBlocker = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: embedded-detached-close
  version: '0.1.0'
do:
  - detached:
      run:
        await: false
        workflow:
          namespace: embedded
          name: child
          version: '0.1.0'
          input: |
            document:
              dsl: '1.0.3'
              namespace: child
              name: child
              version: '0.1.0'
            do:
              - cat-step:
                  call: strij
                  with:
                    type: detached_child
  - blocker:
      call: strij
      with:
        type: blocker
)YAML";

TEST_F(OpenWorkflowTaskHandlerTest, ADetachedSubflowSurvivesConnectionClose) {
  test::FakeChildTaskSubmitter submitter;
  OpenWorkflowTaskHandler handler(submitter, resolver_, test::JqEvaluatorConfig());
  auto state = std::make_shared<test::TestSender::State>();
  const std::string document =
      WriteArtifact("detached-close.yaml", kEmbeddedDetachedWithBlocker);

  handler.HandleTask(MakeTask("wf", document, R"({"v":1})"),
                     std::make_unique<test::TestSender>(state));

  // The detached child is outstanding while the parent awaits a later step.
  ASSERT_EQ(submitter.size(), 2U);
  EXPECT_EQ(submitter.at(0)->task_.type(), "detached_child");
  EXPECT_EQ(submitter.at(1)->task_.type(), "blocker");
  ASSERT_TRUE(static_cast<bool>(state->close_cb_));

  state->close_cb_();

  // The detached child continues after the connection is torn down and produces
  // no result for the parent; the parent's own late outcome is dropped too.
  test::CompleteSuccessAt(submitter, 0, R"("detached")");
  test::CompleteSuccessAt(submitter, 1, R"("late")");
  EXPECT_TRUE(state->sent_.empty());

  // The runner remains usable for the next top-level run.
  auto second = std::make_shared<test::TestSender::State>();
  handler.HandleTask(MakeTask("wf2", WriteArtifact("detached-next.yaml", kEmbeddedDetachedParent),
                              R"({"v":2})"),
                     std::make_unique<test::TestSender>(second));
  ASSERT_EQ(second->sent_.size(), 1U);
  EXPECT_EQ(second->sent_[0].body(), R"({"v":2})");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
