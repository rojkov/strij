#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/core/utils/task_status.hh"
#include "common/task/task.pb.h"
#include "fakes.hh"
#include "gtest/gtest.h"
#include "nodeagent/extensions/task_handlers/openworkflow/interpreter.hh"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

struct Harness {
  Harness() {
    interpreter_ = std::make_shared<WorkflowInterpreter>(
        submitter_, test::JqEvaluatorConfig(), "parent",
        [this](task::TaskResult result) { results_.push_back(std::move(result)); },
        kDefaultSynchronousIterationBudget, nullptr, 0, &event_bus_);
  }

  test::FakeChildTaskSubmitter submitter_;
  test::FakeEventBus event_bus_;
  std::vector<task::TaskResult> results_;
  std::shared_ptr<WorkflowInterpreter> interpreter_;
};

auto EmitDocument(std::string_view event_extra = "", std::string_view task_extra = "")
    -> std::string {
  std::string document =
      "document:\n"
      "  dsl: '1.0.3'\n"
      "  namespace: test\n"
      "  name: emit-test\n"
      "  version: '0.1.0'\n"
      "do:\n"
      "  - publish:\n";
  document.append(task_extra);
  document.append(
      "      emit:\n"
      "        event:\n"
      "          with:\n"
      "            source: https://example.test\n"
      "            type: order.created\n");
  document.append(event_extra);
  return document;
}

TEST(EmitTest, PublishesTheAuthoredEventAndCompletesTheStep) {
  Harness harness;
  harness.interpreter_->Start(EmitDocument("            data:\n              id: '${ .orderId }'\n"),
                              R"({"orderId":7})");

  ASSERT_EQ(harness.results_.size(), 1U);
  ASSERT_TRUE(utils::IsTaskResultOk(harness.results_[0])) << harness.results_[0].body();
  ASSERT_EQ(harness.event_bus_.published_.size(), 1U);
  const event::CloudEvent& published = harness.event_bus_.published_[0];
  EXPECT_EQ(published.specversion(), "1.0");
  EXPECT_FALSE(published.id().empty());
  EXPECT_EQ(published.source(), "https://example.test");
  EXPECT_EQ(published.type(), "order.created");
  EXPECT_EQ(published.datacontenttype(), "application/json");
  EXPECT_FALSE(published.time().empty());
  EXPECT_EQ(published.data(), R"({"id":7})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"({"orderId":7})");
}

TEST(EmitTest, PublishFailureFailsTheStep) {
  Harness harness;
  harness.event_bus_.SetPublishStatus(
      absl::UnavailableError("no event plane configured"));
  harness.interpreter_->Start(EmitDocument("            data: true\n"), R"({})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_UNAVAILABLE);
  EXPECT_NE(harness.results_[0].body().find("publish"), std::string::npos);
}

TEST(EmitTest, SkippedEmitDoesNotPublish) {
  Harness harness;
  harness.interpreter_->Start(EmitDocument("            data: true\n", "      if: '${ false }'\n"),
                              R"({})");

  EXPECT_TRUE(harness.event_bus_.published_.empty());
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_TRUE(utils::IsTaskResultOk(harness.results_[0])) << harness.results_[0].body();
}

TEST(EmitTest, EmitWorksInsideAForBody) {
  Harness harness;
  const std::string document = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: emit-for
  version: '0.1.0'
do:
  - loop:
      for:
        each: item
        in: [1, 2]
      do:
        - publish:
            emit:
              event:
                with:
                  source: https://example.test
                  type: number.seen
                  data: '${ {n: $item} }'
)YAML";

  harness.interpreter_->Start(document, R"({})");

  ASSERT_EQ(harness.results_.size(), 1U);
  ASSERT_TRUE(utils::IsTaskResultOk(harness.results_[0])) << harness.results_[0].body();
  ASSERT_EQ(harness.event_bus_.published_.size(), 2U);
  EXPECT_EQ(harness.event_bus_.published_[0].data(), R"({"n":1})");
  EXPECT_EQ(harness.event_bus_.published_[1].data(), R"({"n":2})");
}

TEST(EmitTest, EmitWorksAsAForkBranch) {
  Harness harness;
  const std::string document = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: emit-fork
  version: '0.1.0'
do:
  - branches:
      fork:
        branches:
          - publish:
              emit:
                event:
                  with:
                    source: https://example.test
                    type: branch.seen
                    data: true
)YAML";

  harness.interpreter_->Start(document, R"({"seed":1})");

  EXPECT_EQ(harness.event_bus_.published_.size(), 1U);
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_TRUE(utils::IsTaskResultOk(harness.results_[0]));
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
