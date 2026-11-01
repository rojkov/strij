#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/core/utils/task_status.hh"
#include "common/event/cloudevent.pb.h"
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

  auto subscription() -> test::FakeEventBus::FakeSubscription* {
    if (event_bus_.subscribers_.empty()) {
      return nullptr;
    }
    return event_bus_.subscribers_.begin()->second->subscription_;
  }

  test::FakeChildTaskSubmitter submitter_;
  test::FakeEventBus event_bus_;
  std::vector<task::TaskResult> results_;
  std::shared_ptr<WorkflowInterpreter> interpreter_;
};

auto makeEvent(std::string id, std::string type, std::string data) -> event::CloudEvent {
  event::CloudEvent event;
  event.set_specversion("1.0");
  event.set_id(std::move(id));
  event.set_source("tests");
  event.set_type(std::move(type));
  event.set_data(std::move(data));
  event.set_datacontenttype("application/json");
  return event;
}

auto OneDocument(std::string read = "") -> std::string {
  std::string document = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: listen-one
  version: '0.1.0'
do:
  - wait:
      listen:
        to:
          one:
            with:
              type: order.created
)YAML";
  if (!read.empty()) {
    document.append("        read: ");
    document.append(read);
    document.append("\n");
  }
  return document;
}

TEST(ListenTest, OneCompletesOnMatchAndAcksNonMatches) {
  Harness harness;
  harness.interpreter_->Start(OneDocument(), R"({})");

  ASSERT_NE(harness.subscription(), nullptr);
  auto* subscription = harness.subscription();

  harness.event_bus_.Deliver("parent/wait/0", makeEvent("e1", "other", R"({"n":0})"), "t1");
  EXPECT_TRUE(harness.results_.empty());
  ASSERT_EQ(subscription->acked_.size(), 1U);
  EXPECT_EQ(subscription->acked_[0], "t1");

  harness.event_bus_.Deliver("parent/wait/0",
                             makeEvent("e2", "order.created", R"({"n":1})"), "t2");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_TRUE(utils::IsTaskResultOk(harness.results_[0]));
  EXPECT_EQ(harness.results_[0].body(), R"([{"n":1}])");
  EXPECT_TRUE(harness.event_bus_.subscribers_.empty());
}

TEST(ListenTest, AllWaitsForEveryFilterInDeclarationOrder) {
  Harness harness;
  const std::string document = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: listen-all
  version: '0.1.0'
do:
  - wait:
      listen:
        to:
          all:
            - with:
                type: vitals.temperature
            - with:
                type: vitals.bpm
)YAML";
  harness.interpreter_->Start(document, R"({})");
  ASSERT_NE(harness.subscription(), nullptr);

  harness.event_bus_.Deliver("parent/wait/0",
                             makeEvent("e1", "vitals.temperature", R"({"t":39})"), "t1");
  EXPECT_TRUE(harness.results_.empty());

  harness.event_bus_.Deliver("parent/wait/0", makeEvent("e2", "vitals.bpm", R"({"b":80})"),
                             "t2");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"([{"t":39},{"b":80}])");
}

TEST(ListenTest, AnyUntilCompletesAfterTheCondition) {
  Harness harness;
  const std::string document = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: listen-any
  version: '0.1.0'
do:
  - wait:
      listen:
        to:
          any:
            - with:
                type: tick
          until: '${ (. | length) > 3 }'
)YAML";
  harness.interpreter_->Start(document, R"({})");
  ASSERT_NE(harness.subscription(), nullptr);

  for (int index = 0; index < 4; ++index) {
    harness.event_bus_.Deliver("parent/wait/0", makeEvent("e" + std::to_string(index), "tick", "1"),
                               "t" + std::to_string(index));
  }

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "[1,1,1,1]");
}

TEST(ListenTest, ReadEnvelopeSelectsTheFullEvent) {
  Harness harness;
  harness.interpreter_->Start(OneDocument("envelope"), R"({})");
  ASSERT_NE(harness.subscription(), nullptr);

  harness.event_bus_.Deliver("parent/wait/0",
                             makeEvent("e1", "order.created", R"({"n":1})"), "t1");

  ASSERT_EQ(harness.results_.size(), 1U);
  const std::string& body = harness.results_[0].body();
  EXPECT_NE(body.find(R"("id":"e1")"), std::string::npos);
  EXPECT_NE(body.find(R"("type":"order.created")"), std::string::npos);
  EXPECT_NE(body.find(R"("data":{"n":1})"), std::string::npos);
}


TEST(ListenTest, DataPredicateAndCorrelationAreApplied) {
  Harness harness;
  const std::string document = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: listen-filter
  version: '0.1.0'
do:
  - wait:
      listen:
        to:
          one:
            with:
              type: vitals.temperature
              data: '${ .temperature > 38 }'
            correlate:
              roomId:
                from: .roomid
)YAML";
  harness.interpreter_->Start(document, R"({})");
  ASSERT_NE(harness.subscription(), nullptr);

  event::CloudEvent wrong_room = makeEvent("e1", "vitals.temperature",
                                           R"({"temperature":39,"roomid":"r1"})");
  (*wrong_room.mutable_attributes())["roomId"] = "r2";
  harness.event_bus_.Deliver("parent/wait/0", wrong_room, "t1");
  EXPECT_TRUE(harness.results_.empty());

  event::CloudEvent cold = makeEvent("e2", "vitals.temperature",
                                     R"({"temperature":37,"roomid":"r1"})");
  (*cold.mutable_attributes())["roomId"] = "r1";
  harness.event_bus_.Deliver("parent/wait/0", cold, "t2");
  EXPECT_TRUE(harness.results_.empty());

  event::CloudEvent hot = makeEvent("e3", "vitals.temperature",
                                    R"({"temperature":39,"roomid":"r1"})");
  (*hot.mutable_attributes())["roomId"] = "r1";
  harness.event_bus_.Deliver("parent/wait/0", hot, "t3");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), R"([{"temperature":39,"roomid":"r1"}])");
}

TEST(ListenTest, DuplicateEventIsProcessedOnce) {
  Harness harness;
  const std::string document = R"YAML(document:
  dsl: '1.0.3'
  namespace: test
  name: listen-dedup
  version: '0.1.0'
do:
  - wait:
      listen:
        to:
          any:
            - with:
                type: tick
          until: '${ (. | length) > 1 }'
)YAML";
  harness.interpreter_->Start(document, R"({})");
  ASSERT_NE(harness.subscription(), nullptr);

  harness.event_bus_.Deliver("parent/wait/0", makeEvent("e1", "tick", "1"), "t1");
  EXPECT_TRUE(harness.results_.empty());
  harness.event_bus_.Deliver("parent/wait/0", makeEvent("e1", "tick", "1"), "t2");
  EXPECT_TRUE(harness.results_.empty());

  harness.event_bus_.Deliver("parent/wait/0", makeEvent("e2", "tick", "1"), "t3");
  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].body(), "[1,1]");
}

TEST(ListenTest, InvalidReadModeFailsValidation) {
  Harness harness;
  harness.interpreter_->Start(OneDocument("bogus"), R"({})");

  ASSERT_EQ(harness.results_.size(), 1U);
  EXPECT_EQ(harness.results_[0].status(), task::TASK_STATUS_MALFORMED_REQUEST);
  EXPECT_TRUE(harness.event_bus_.subscribers_.empty());
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent::task_handlers::openworkflow
