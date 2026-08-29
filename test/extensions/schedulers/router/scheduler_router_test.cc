#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"
#include "test/mocks/extensions/extensions_mocks.hh"

#include "core/config/gateway.pb.h"
#include "core/gateway/node_directory.hh"
#include "core/gateway/result_receiver_storage.hh"
#include "core/io/connection.hh"
#include "core/io/protocol_parser.hh"
#include "core/io/tlv_frame.hh"
#include "absl/status/status.h"
#include "core/task/task.pb.h"
#include "extensions/schedulers/round_robin/round_robin_scheduler.hh"
#include "extensions/schedulers/router/scheduler_router.hh"
#include "extensions/schedulers/scheduler.hh"
#include "gtest/gtest.h"

namespace strij::extensions::schedulers {
namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;

class StubScheduler final : public Scheduler {
public:
  StubScheduler(std::string_view protocol, std::initializer_list<uint8_t> handled_types = {})
      : protocol_{protocol}, handled_types_{handled_types} {}

  void Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) override {
    scheduled_types_.push_back(task.type());
    held_receivers_.push_back(std::move(receiver));
  }
  [[nodiscard]] auto RequiredProtocol() const -> std::string_view override { return protocol_; }
  [[nodiscard]] auto HandleFrame(io::TlvFrame frame, io::Connection& /*conn*/)
      -> absl::Status override {
    handled_frames_.push_back(frame.type_id);
    return absl::OkStatus();
  }
  [[nodiscard]] auto HandledFrameTypes() const -> std::span<const uint8_t> override {
    return handled_types_;
  }

  std::vector<std::string> scheduled_types_;
  std::vector<uint8_t> handled_frames_;
  std::vector<gateway::ResultReceiverPtr> held_receivers_;

private:
  std::string protocol_;
  std::vector<uint8_t> handled_types_;
};

class RecordingReceiver final : public gateway::ResultReceiver {
public:
  explicit RecordingReceiver(std::shared_ptr<std::vector<std::string>> errors)
      : errors_{std::move(errors)} {}

  void Deliver(std::span<const std::byte> /*value*/, bool /*is_final*/) override {}
  void DeliverError(std::string_view reason) override { errors_->emplace_back(reason); }

  std::shared_ptr<std::vector<std::string>> errors_;
};

auto MakeReceiver() -> std::pair<gateway::ResultReceiverPtr, std::shared_ptr<std::vector<std::string>>> {
  auto errors = std::make_shared<std::vector<std::string>>();
  auto receiver = std::make_unique<RecordingReceiver>(errors);
  return {std::move(receiver), std::move(errors)};
}

auto MakeTask(const std::string& id, const std::string& type) -> task::Task {
  task::Task task;
  task.set_id(id);
  task.set_type(type);
  return task;
}

auto Routed(std::string task_type, SchedulerPtr scheduler) -> SchedulerRouter::RoutedScheduler {
  return SchedulerRouter::RoutedScheduler{.scheduler = std::move(scheduler),
                                          .task_type = std::move(task_type)};
}

template <typename... Entries>
auto MakeRouter(Entries... entries) -> std::unique_ptr<SchedulerRouter> {
  std::vector<SchedulerRouter::RoutedScheduler> routed;
  (routed.push_back(std::move(entries)), ...);
  return std::make_unique<SchedulerRouter>(std::move(routed));
}

class SchedulerRouterTest : public ::testing::Test {
protected:
  std::shared_ptr<event::MockDispatcher> dispatcher_{std::make_shared<event::MockDispatcher>()};
  gateway::ResultReceiverStorage storage_;

  auto MakeConnectedDirectory(std::initializer_list<std::string> ids)
      -> std::unique_ptr<gateway::NodeDirectory> {
    auto directory = std::make_unique<gateway::NodeDirectory>(
        dispatcher_,
        [](io::Connection&) -> io::ProtocolParserPtr {
          return std::make_unique<io::TrivialParser>();
        },
        storage_);
    EXPECT_CALL(*dispatcher_, PrepareConnect(_, _, _, _, _)).WillRepeatedly(Return());
    EXPECT_CALL(*dispatcher_, PrepareRead(_, _, _, _, _)).WillRepeatedly(Return());
    for (const auto& node_id : ids) {
      directory->AddNode(node_id, "10.0.0.1:9090");
    }
    for (const auto& node_id : ids) {
      directory->GetNode(node_id)->HandleCompletion(0, 0, 0);
    }
    return directory;
  }

  void WireContext(extensions::MockGatewayFactoryContext& context,
                   gateway::NodeDirectory& directory) {
    ON_CALL(context, NodeDirectory()).WillByDefault(ReturnRef(directory));
    ON_CALL(context, ResultReceiverStorage()).WillByDefault(ReturnRef(storage_));
  }
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(SchedulerRouterTest, RoutesByExactTaskTypeAndFallsBackToDefault) {
  auto* echo = new StubScheduler("push");
  auto* cv = new StubScheduler("push");
  auto* fallback = new StubScheduler("push");
  auto router =
      MakeRouter(Routed("echo", SchedulerPtr(echo)), Routed("cv", SchedulerPtr(cv)),
                 Routed("", SchedulerPtr(fallback)));

  router->Schedule(MakeTask("1", "echo"), MakeReceiver().first);
  router->Schedule(MakeTask("2", "cv"), MakeReceiver().first);
  router->Schedule(MakeTask("3", "other"), MakeReceiver().first);

  ASSERT_EQ(echo->scheduled_types_.size(), 1U);
  EXPECT_EQ(echo->scheduled_types_.front(), "echo");
  ASSERT_EQ(cv->scheduled_types_.size(), 1U);
  EXPECT_EQ(cv->scheduled_types_.front(), "cv");
  ASSERT_EQ(fallback->scheduled_types_.size(), 1U);
  EXPECT_EQ(fallback->scheduled_types_.front(), "other");
  EXPECT_EQ(router->RoutedSchedulerCount(), 3U);
}

TEST_F(SchedulerRouterTest, DeliversErrorWhenNoMatchAndNoDefault) {
  auto* echo = new StubScheduler("push");
  auto router = MakeRouter(Routed("echo", SchedulerPtr(echo)));

  auto receiver = MakeReceiver();
  router->Schedule(MakeTask("1", "unknown"), std::move(receiver.first));

  ASSERT_EQ(receiver.second->size(), 1U);
  EXPECT_NE(receiver.second->at(0).find("no scheduler configured"), std::string::npos);
}

TEST_F(SchedulerRouterTest, RoutesFramesToOwningScheduler) {
  auto* a = new StubScheduler("push", {0});
  auto* b = new StubScheduler("push", {2});
  auto router = MakeRouter(Routed("echo", SchedulerPtr(a)), Routed("", SchedulerPtr(b)));

  EXPECT_EQ(router->HandledFrameTypes().size(), 2U);
  EXPECT_EQ(router->HandledFrameTypes()[0], 0U);
  EXPECT_EQ(router->HandledFrameTypes()[1], 2U);

  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher_,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  io::Connection conn(fds[0], dispatcher_, &owner,
                      [](io::Connection&) -> io::ProtocolParserPtr {
                        return std::make_unique<io::TrivialParser>();
                      });

  EXPECT_TRUE(router->HandleFrame(io::TlvFrame{0, {}}, conn).ok());
  EXPECT_TRUE(router->HandleFrame(io::TlvFrame{2, {}}, conn).ok());

  EXPECT_FALSE(router->HandleFrame(io::TlvFrame{5, {}}, conn).ok());

  ASSERT_EQ(a->handled_frames_.size(), 1U);
  EXPECT_EQ(a->handled_frames_[0], 0U);
  ASSERT_EQ(b->handled_frames_.size(), 1U);
  EXPECT_EQ(b->handled_frames_[0], 2U);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(SchedulerRouterTest, RequiredProtocolComesFromConstituents) {
  auto router = MakeRouter(Routed("echo", SchedulerPtr(new StubScheduler("push"))),
                           Routed("", SchedulerPtr(new StubScheduler("probe"))));
  EXPECT_EQ(router->RequiredProtocol(), "push");
}

TEST_F(SchedulerRouterTest, BuildSchedulerRouterFromConfig) {
  auto directory = MakeConnectedDirectory({"A"});
  extensions::MockGatewayFactoryContext context;
  WireContext(context, *directory);

  config::GatewayConfig config;
  auto* scheduler_config = config.add_schedulers();
  scheduler_config->mutable_extension()->set_name("round_robin");
  scheduler_config->set_task_type("echo");

  auto router = BuildSchedulerRouter(config, context);
  ASSERT_TRUE(router.ok()) << router.status().message();
  EXPECT_EQ((*router)->RoutedSchedulerCount(), 1U);
  EXPECT_EQ((*router)->RequiredProtocol(), "push");
}

TEST_F(SchedulerRouterTest, BuildSchedulerRouterRejectsEmpty) {
  extensions::MockGatewayFactoryContext context;
  config::GatewayConfig config;
  EXPECT_FALSE(BuildSchedulerRouter(config, context).ok());
}

TEST_F(SchedulerRouterTest, BuildSchedulerRouterRejectsUnknownScheduler) {
  extensions::MockGatewayFactoryContext context;
  config::GatewayConfig config;
  config.add_schedulers()->mutable_extension()->set_name("no_such_scheduler");
  EXPECT_FALSE(BuildSchedulerRouter(config, context).ok());
}

TEST_F(SchedulerRouterTest, BuildSchedulerRouterRejectsDuplicateTaskType) {
  auto directory = MakeConnectedDirectory({"A"});
  extensions::MockGatewayFactoryContext context;
  WireContext(context, *directory);
  config::GatewayConfig config;
  config.add_schedulers()->mutable_extension()->set_name("round_robin");
  config.add_schedulers()->mutable_extension()->set_name("round_robin");
  for (int i = 0; i < config.schedulers_size(); ++i) {
    config.mutable_schedulers(i)->set_task_type("echo");
  }
  auto router = BuildSchedulerRouter(config, context);
  EXPECT_FALSE(router.ok());
  EXPECT_NE(router.status().message().find("more than once"), std::string::npos);
}

TEST_F(SchedulerRouterTest, BuildSchedulerRouterRejectsMultipleDefaults) {
  auto directory = MakeConnectedDirectory({"A"});
  extensions::MockGatewayFactoryContext context;
  WireContext(context, *directory);
  config::GatewayConfig config;
  config.add_schedulers()->mutable_extension()->set_name("round_robin");
  config.add_schedulers()->mutable_extension()->set_name("round_robin");
  auto router = BuildSchedulerRouter(config, context);
  EXPECT_FALSE(router.ok());
  EXPECT_NE(router.status().message().find("more than one default"), std::string::npos);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::extensions::schedulers