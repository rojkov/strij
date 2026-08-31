#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "absl/status/status.h"

#include "gateway/core/exact_state_tracker.hh"
#include "gateway/core/gateway_http_handler.hh"
#include "gateway/core/gateway_tlv_handler.hh"
#include "gateway/core/http_result_receiver.hh"
#include "gateway/core/requirements_resolver.hh"
#include "gateway/core/result_receiver_storage.hh"
#include "common/core/io/connection.hh"
#include "common/core/io/protocol_parser.hh"
#include "common/core/io/tlv_frame.hh"
#include "common/core/io/tlv_parser.hh"
#include "common/node/capabilities.pb.h"
#include "common/task/task.pb.h"
#include "gateway/extensions/schedulers/round_robin/round_robin_scheduler.hh"
#include "google/protobuf/map.h"
#include "gtest/gtest.h"

namespace strij::gateway {
namespace {

class MockReceiver : public ResultReceiver {
public:
  explicit MockReceiver(std::vector<std::byte>* out,
                        std::shared_ptr<std::vector<bool>> finalities = {},
                        std::shared_ptr<std::vector<std::string>> errors = {})
      : out_{out}, finalities_{std::move(finalities)}, errors_{std::move(errors)} {}

  void Deliver(std::span<const std::byte> value, bool is_final) override {
    out_->assign(value.begin(), value.end());
    if (finalities_) {
      finalities_->push_back(is_final);
    }
  }

  void DeliverError(std::string_view reason) override {
    if (errors_) {
      errors_->push_back(std::string(reason));
    }
  }

private:
  std::vector<std::byte>* out_;
  std::shared_ptr<std::vector<bool>> finalities_;
  std::shared_ptr<std::vector<std::string>> errors_;
};

class NullReceiver : public ResultReceiver {
public:
  void Deliver(std::span<const std::byte> /*value*/, bool /*is_final*/) override {}
  void DeliverError(std::string_view /*reason*/) override {}
};

// Deterministic scheduler for handler wiring tests: records every task it is
// asked to schedule, writes the submission frame to a fixed node's connection
// (or declines with an error), and registers the receiver in the given storage
// when a node is selected. Records by value so recorded data stays valid after
// HandleMessage returns.
class StubScheduler : public extensions::Scheduler {
public:
  StubScheduler(gateway::Node* node, gateway::ResultReceiverStorage* storage,
                std::vector<task::Task>* recorded)
      : node_{node}, storage_{storage}, recorded_{recorded} {}

  void Schedule(const task::Task& task, gateway::ResultReceiverPtr receiver) override {
    if (recorded_ != nullptr) {
      recorded_->push_back(task);
    }
    if (node_ == nullptr) {
      receiver->DeliverError("stub scheduler declined");
      return;
    }
    if (storage_ != nullptr) {
      storage_->Put(task.id(), std::move(receiver), std::string(node_->GetNodeId()));
    } else {
      (void)receiver;
    }

    std::string serialized;
    task.SerializeToString(&serialized);
    auto frame = io::SerializeTlvFrame(
        io::TlvFrame::kTaskSubmission,
        std::as_bytes(std::span(serialized.data(), serialized.size())));
    node_->GetConnection()->Write(frame);
  }

  auto RequiredProtocol() const -> std::string_view override { return "push"; }
  [[nodiscard]] auto HandleFrame(io::TlvFrame /*frame*/, io::Connection& /*conn*/)
      -> absl::Status override {
    return absl::OkStatus();
  }
  auto HandledFrameTypes() const -> std::span<const uint8_t> override { return {}; }

private:
  gateway::Node* node_;
  gateway::ResultReceiverStorage* storage_;
  std::vector<task::Task>* recorded_;
};

auto SerializeTaskResult(const task::TaskResult& result) -> std::string {
  std::string serialized;
  result.SerializeToString(&serialized);
  return serialized;
}

auto toTlvFrame(std::string_view serialized) -> io::TlvFrame {
  return {.type_id = io::TlvFrame::kResult,
          .value = std::as_bytes(std::span(serialized.data(), serialized.size()))};
}

class ParseTaskTypeTest : public ::testing::Test {};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(ParseTaskTypeTest, ParsesTypeFromTaskPath) {
  auto type = ParseTaskType("/tasks/echo");
  ASSERT_TRUE(type.has_value());
  EXPECT_EQ(*type, "echo");
}

TEST_F(ParseTaskTypeTest, StripsQueryString) {
  auto type = ParseTaskType("/tasks/echo?param=1");
  ASSERT_TRUE(type.has_value());
  EXPECT_EQ(*type, "echo");
}

TEST_F(ParseTaskTypeTest, RejectsPathWithoutTaskPrefix) {
  auto type = ParseTaskType("/not-a-task");
  EXPECT_FALSE(type.has_value());
}

TEST_F(ParseTaskTypeTest, EmptyTypeForBareTasksPrefix) {
  auto type = ParseTaskType("/tasks/");
  ASSERT_TRUE(type.has_value());
  EXPECT_TRUE(type->empty());
}

TEST_F(ParseTaskTypeTest, EmptyTypeForTasksPrefixWithQuery) {
  auto type = ParseTaskType("/tasks/?param=1");
  ASSERT_TRUE(type.has_value());
  EXPECT_TRUE(type->empty());
}

class GatewayTlvHandlerTest : public ::testing::Test {
protected:
  ResultReceiverStorage storage_;
  std::shared_ptr<event::MockDispatcher> dispatcher_{std::make_shared<event::MockDispatcher>()};
  NodeDirectory directory_{dispatcher_,
                           [](io::Connection&) -> io::ProtocolParserPtr {
                             return std::make_unique<io::TrivialParser>();
                           },
                           storage_};
  GatewayTlvHandler handler_{directory_, storage_};
};

TEST_F(GatewayTlvHandlerTest, NodeAdvertisementStoresCapabilitiesAndRekeyes) {
  event::Completable* node_completable = nullptr;
  EXPECT_CALL(*dispatcher_,
              PrepareConnect(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SaveArg<0>(&node_completable), ::testing::Return()));
  directory_.AddNode("10.0.0.1:9090", "10.0.0.1:9090");
  ASSERT_NE(node_completable, nullptr);
  // The Node's Connection constructor issues a PrepareRead.
  EXPECT_CALL(*dispatcher_,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  node_completable->HandleCompletion(0, 0, 0);

  auto* node = directory_.GetNode("10.0.0.1:9090");
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->GetStatus(), Node::Status::kConnected);

  node::NodeCapabilities caps;
  caps.set_node_id("node-realkey");
  caps.set_capability_version(1);
  caps.add_scheduling_protocols()->set_name("push");
  std::string serialized;
  ASSERT_TRUE(caps.SerializeToString(&serialized));

  auto status = handler_.HandleFrame(
      {.type_id = io::TlvFrame::kNodeAdvertisement, .value = std::as_bytes(std::span(serialized))},
      *node->GetConnection());

  // The record was rekeyed to the advertised identity.
  EXPECT_EQ(directory_.GetNode("10.0.0.1:9090"), nullptr);
  auto* rekeyed = directory_.GetNode("node-realkey");
  ASSERT_NE(rekeyed, nullptr);
  EXPECT_EQ(rekeyed->GetNodeId(), "node-realkey");
  ASSERT_NE(rekeyed->GetCapabilities(), nullptr);
  EXPECT_EQ(rekeyed->GetCapabilities()->node_id(), "node-realkey");
}

TEST_F(GatewayTlvHandlerTest, NodeStateFrameUpdatesNodeState) {
  event::Completable* node_completable = nullptr;
  EXPECT_CALL(*dispatcher_,
              PrepareConnect(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SaveArg<0>(&node_completable), ::testing::Return()));
  directory_.AddNode("n1", "10.0.0.1:9090");
  ASSERT_NE(node_completable, nullptr);
  EXPECT_CALL(*dispatcher_,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  node_completable->HandleCompletion(0, 0, 0);

  auto* node = directory_.GetNode("n1");
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->GetState(), nullptr);

  node::NodeState state;
  state.set_node_id("n1");
  state.set_seq(7);
  state.set_in_flight(3);
  state.add_pools()->set_in_use(2);
  std::string serialized;
  ASSERT_TRUE(state.SerializeToString(&serialized));

  auto status = handler_.HandleFrame(
      {.type_id = io::TlvFrame::kNodeState, .value = std::as_bytes(std::span(serialized))},
      *node->GetConnection());

  ASSERT_NE(node->GetState(), nullptr);
  EXPECT_EQ(node->GetState()->seq(), 7U);
  EXPECT_EQ(node->GetState()->in_flight(), 3U);
  EXPECT_EQ(node->GetState()->pools_size(), 1);
}

class ParamsOnlyRequirementsResolverTest : public ::testing::Test {
protected:
  ParamsOnlyRequirementsResolver resolver_;
};

TEST_F(ParamsOnlyRequirementsResolverTest, ReadsDeclaredResourceEntries) {
  google::protobuf::Map<std::string, std::string> parameters;
  parameters["resources.cpu"] = "2";
  parameters["resources.gpu.h100"] = "1";
  parameters["function"] = "/usr/bin/cat";

  auto requirements = resolver_.Resolve(FunctionRef{.type = "echo"}, parameters);

  EXPECT_EQ(requirements.resources().size(), 2U);
  EXPECT_EQ(requirements.resources().at("cpu"), 2U);
  EXPECT_EQ(requirements.resources().at("gpu.h100"), 1U);
}

TEST_F(ParamsOnlyRequirementsResolverTest, AcceptsDashSeparatedKeys) {
  google::protobuf::Map<std::string, std::string> parameters;
  parameters["resources-cpu"] = "4";

  auto requirements = resolver_.Resolve(FunctionRef{.type = "echo"}, parameters);

  EXPECT_EQ(requirements.resources().size(), 1U);
  EXPECT_EQ(requirements.resources().at("cpu"), 4U);
}

TEST_F(ParamsOnlyRequirementsResolverTest, ReturnsEmptyWhenNoResourcesDeclared) {
  google::protobuf::Map<std::string, std::string> parameters;
  parameters["function"] = "/usr/bin/cat";

  auto requirements = resolver_.Resolve(FunctionRef{.type = "echo"}, parameters);

  EXPECT_EQ(requirements.resources().size(), 0U);
}

TEST_F(GatewayTlvHandlerTest, DispatchResultToReceiver) {
  std::vector<std::byte> delivered;
  storage_.Put("42", std::make_unique<MockReceiver>(&delivered), "node-1");

  task::TaskResult result;
  result.set_id("42");
  result.set_body("CCDD");
  std::string serialized;
  result.SerializeToString(&serialized);
  auto wire = io::SerializeTlvFrame(io::TlvFrame::kResult,
                                    std::as_bytes(std::span(serialized.data(), serialized.size())));

  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  io::Connection conn(fds[0], dispatcher, &owner,
                      [](io::Connection&) -> std::unique_ptr<io::ProtocolParser> {
                        return std::make_unique<io::TrivialParser>();
                      });

  // Reconstruct the frame from wire bytes and deliver through the handler
  std::vector<io::TlvFrame> received_frames;
  io::TlvParser parser(
      [&received_frames](io::TlvFrame frame) { received_frames.push_back(frame); });
  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), wire.data(), wire.size());
  parser.OnData(wire.size());
  ASSERT_EQ(received_frames.size(), 1U);

  auto status = handler_.HandleFrame(received_frames[0], conn);

  auto expected =
      std::vector<std::byte>{std::byte{'C'}, std::byte{'C'}, std::byte{'D'}, std::byte{'D'}};
  ASSERT_EQ(delivered.size(), expected.size());
  EXPECT_TRUE(std::equal(delivered.begin(), delivered.end(), expected.begin()));
  EXPECT_EQ(storage_.Get("42"), nullptr);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(GatewayTlvHandlerTest, UnknownTaskIdDropsResult) {
  std::vector<std::byte> delivered;
  storage_.Put("1", std::make_unique<MockReceiver>(&delivered), "node-1");

  task::TaskResult result;
  result.set_id("99");
  result.set_body("data");
  std::string serialized;
  result.SerializeToString(&serialized);
  auto wire = io::SerializeTlvFrame(io::TlvFrame::kResult,
                                    std::as_bytes(std::span(serialized.data(), serialized.size())));

  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  io::Connection conn(fds[0], dispatcher, &owner,
                      [](io::Connection&) -> std::unique_ptr<io::ProtocolParser> {
                        return std::make_unique<io::TrivialParser>();
                      });

  std::vector<io::TlvFrame> received_frames;
  io::TlvParser parser(
      [&received_frames](io::TlvFrame frame) { received_frames.push_back(frame); });
  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), wire.data(), wire.size());
  parser.OnData(wire.size());
  ASSERT_EQ(received_frames.size(), 1U);

  auto status = handler_.HandleFrame(received_frames[0], conn);

  EXPECT_TRUE(delivered.empty());
  EXPECT_NE(storage_.Get("1"), nullptr);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(GatewayTlvHandlerTest, MalformedResultFrameIsDropped) {
  std::vector<std::byte> delivered;
  storage_.Put("7", std::make_unique<MockReceiver>(&delivered), "node-1");

  auto garbage =
      std::vector<std::byte>{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
  auto wire = io::SerializeTlvFrame(io::TlvFrame::kResult, garbage);

  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  io::Connection conn(fds[0], dispatcher, &owner, [](io::Connection&) -> io::ProtocolParserPtr {
    return std::make_unique<io::TrivialParser>();
  });

  std::vector<io::TlvFrame> received_frames;
  io::TlvParser parser(
      [&received_frames](io::TlvFrame frame) { received_frames.push_back(frame); });
  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), wire.data(), wire.size());
  parser.OnData(wire.size());
  ASSERT_EQ(received_frames.size(), 1U);

  auto status = handler_.HandleFrame(received_frames[0], conn);

  EXPECT_TRUE(delivered.empty());
  EXPECT_NE(storage_.Get("7"), nullptr);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(GatewayTlvHandlerTest, RejectedTaskRoutesErrorToReceiver) {
  auto errors = std::make_shared<std::vector<std::string>>();
  storage_.Put("t1", std::make_unique<MockReceiver>(nullptr, nullptr, errors), "node-1");

  task::TaskRejected rejected;
  rejected.set_id("t1");
  rejected.set_reason("gpu.h100 exhausted");
  std::string serialized;
  ASSERT_TRUE(rejected.SerializeToString(&serialized));

  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  io::Connection conn(fds[0], dispatcher, &owner, [](io::Connection&) -> io::ProtocolParserPtr {
    return std::make_unique<io::TrivialParser>();
  });

  auto status = handler_.HandleFrame(
      {.type_id = io::TlvFrame::kTaskRejected, .value = std::as_bytes(std::span(serialized))},
      conn);

  ASSERT_EQ(errors->size(), 1U);
  EXPECT_EQ((*errors)[0], "gpu.h100 exhausted");
  EXPECT_EQ(storage_.Get("t1"), nullptr);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(GatewayTlvHandlerTest, RejectedTaskWithoutReceiverIsDropped) {
  auto errors = std::make_shared<std::vector<std::string>>();
  storage_.Put("t1", std::make_unique<MockReceiver>(nullptr, nullptr, errors), "node-1");

  task::TaskRejected rejected;
  rejected.set_id("unknown");
  rejected.set_reason("concurrency at capacity");
  std::string serialized;
  ASSERT_TRUE(rejected.SerializeToString(&serialized));

  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  io::Connection conn(fds[0], dispatcher, &owner, [](io::Connection&) -> io::ProtocolParserPtr {
    return std::make_unique<io::TrivialParser>();
  });

  auto status = handler_.HandleFrame(
      {.type_id = io::TlvFrame::kTaskRejected, .value = std::as_bytes(std::span(serialized))},
      conn);

  EXPECT_TRUE(errors->empty());
  EXPECT_NE(storage_.Get("t1"), nullptr);

  close(fds[0]);
  close(fds[1]);
}

TEST(HttpResponseFramerTest, ErrorResponseUses503) {
  HttpResponseFramer framer;
  auto frames = framer.ErrorResponse("gpu.h100 exhausted");

  ASSERT_EQ(frames.size(), 1U);
  std::string response(std::bit_cast<const char*>(frames[0].data()), frames[0].size());
  EXPECT_NE(response.find("HTTP/1.1 503 Service Unavailable"), std::string::npos);
  EXPECT_NE(response.find("gpu.h100 exhausted"), std::string::npos);

  // A subsequent result must not produce further frames (connection is done).
  std::vector<std::byte> body{std::byte{'x'}};
  EXPECT_TRUE(framer.Next(body, true).empty());
}

TEST_F(GatewayTlvHandlerTest, IntermediateResultKeepsReceiverUntilFinal) {
  std::vector<std::byte> delivered;
  auto finalities = std::make_shared<std::vector<bool>>();
  auto receiver = std::make_unique<MockReceiver>(&delivered, finalities);
  auto* receiver_raw = receiver.get();
  storage_.Put("42", std::move(receiver), "node-1");

  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillRepeatedly(::testing::Return());
  io::Connection conn(fds[0], dispatcher, &owner, [](io::Connection&) -> io::ProtocolParserPtr {
    return std::make_unique<io::TrivialParser>();
  });

  task::TaskResult intermediate;
  intermediate.set_id("42");
  intermediate.set_body("chunk");
  intermediate.set_is_final(false);
  auto intermediate_serialized = SerializeTaskResult(intermediate);
  auto status = handler_.HandleFrame(toTlvFrame(intermediate_serialized), conn);
  EXPECT_EQ(storage_.Get("42"), receiver_raw);
  ASSERT_EQ(finalities->size(), 1U);
  EXPECT_FALSE((*finalities)[0]);

  task::TaskResult final_result;
  final_result.set_id("42");
  final_result.set_body("tail");
  final_result.set_is_final(true);
  auto final_serialized = SerializeTaskResult(final_result);
  status = handler_.HandleFrame(toTlvFrame(final_serialized), conn);
  EXPECT_EQ(storage_.Get("42"), nullptr);
  ASSERT_EQ(finalities->size(), 2U);
  EXPECT_TRUE((*finalities)[1]);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(GatewayTlvHandlerTest, AbsentIsFinalFieldTreatsResultAsFinal) {
  std::vector<std::byte> delivered;
  auto receiver = std::make_unique<MockReceiver>(&delivered);
  storage_.Put("7", std::move(receiver), "node-1");

  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillRepeatedly(::testing::Return());
  io::Connection conn(fds[0], dispatcher, &owner, [](io::Connection&) -> io::ProtocolParserPtr {
    return std::make_unique<io::TrivialParser>();
  });

  task::TaskResult result;
  result.set_id("7");
  result.set_body("done");
  auto result_serialized = SerializeTaskResult(result);
  auto status = handler_.HandleFrame(toTlvFrame(result_serialized), conn);
  EXPECT_EQ(storage_.Get("7"), nullptr);

  close(fds[0]);
  close(fds[1]);
}

TEST(HttpResponseFramerTest, SingleShotUsesContentLength) {
  HttpResponseFramer framer;
  std::vector<std::byte> body = {std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};

  auto frames = framer.Next(body, true);

  ASSERT_EQ(frames.size(), 1U);
  std::string response(std::bit_cast<const char*>(frames[0].data()), frames[0].size());
  EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
  EXPECT_NE(response.find("Content-Length: 3"), std::string::npos);
  EXPECT_EQ(response.find("Transfer-Encoding: chunked"), std::string::npos);
  EXPECT_EQ(response.find("0\r\n\r\n"), std::string::npos);
  EXPECT_EQ(response.back(), 'c');

  std::vector<std::byte> empty;
  EXPECT_TRUE(framer.Next(empty, true).empty());
}

TEST(HttpResponseFramerTest, StreamingUsesChunkedEncoding) {
  HttpResponseFramer framer;
  std::vector<std::byte> a_bytes{std::byte{'a'}};
  std::vector<std::byte> b_bytes{std::byte{'b'}};
  std::vector<std::byte> c_bytes{std::byte{'c'}};

  auto first = framer.Next(a_bytes, false);
  ASSERT_EQ(first.size(), 2U);
  std::string header(std::bit_cast<const char*>(first[0].data()), first[0].size());
  EXPECT_NE(header.find("HTTP/1.1 200 OK"), std::string::npos);
  EXPECT_NE(header.find("Transfer-Encoding: chunked"), std::string::npos);
  EXPECT_EQ(header.find("Content-Length"), std::string::npos);
  std::string chunk1(std::bit_cast<const char*>(first[1].data()), first[1].size());
  EXPECT_EQ(chunk1, "1\r\na\r\n");

  auto second = framer.Next(b_bytes, false);
  ASSERT_EQ(second.size(), 1U);
  std::string chunk2(std::bit_cast<const char*>(second[0].data()), second[0].size());
  EXPECT_EQ(chunk2, "1\r\nb\r\n");

  auto third = framer.Next(c_bytes, true);
  ASSERT_EQ(third.size(), 2U);
  std::string chunk3(std::bit_cast<const char*>(third[0].data()), third[0].size());
  EXPECT_EQ(chunk3, "1\r\nc\r\n");
  std::string terminal(std::bit_cast<const char*>(third[1].data()), third[1].size());
  EXPECT_EQ(terminal, "0\r\n\r\n");

  std::vector<std::byte> empty;
  EXPECT_TRUE(framer.Next(empty, true).empty());
}

TEST(HttpResponseFramerTest, EmptyStreamingBodyProducesOnlyTerminal) {
  HttpResponseFramer framer;
  std::vector<std::byte> empty;

  auto first = framer.Next(empty, false);
  ASSERT_EQ(first.size(), 2U);

  auto final_result = framer.Next(empty, true);
  ASSERT_EQ(final_result.size(), 2U);
  std::string chunk(std::bit_cast<const char*>(final_result[0].data()), final_result[0].size());
  EXPECT_EQ(chunk, "0\r\n\r\n");
  std::string terminal(std::bit_cast<const char*>(final_result[1].data()), final_result[1].size());
  EXPECT_EQ(terminal, "0\r\n\r\n");
}

TEST(PopulateParametersFromHeadersTest, XStrijHeaderMapsToParameter) {
  task::Task task;
  PopulateParametersFromHeaders(task, {{"x-strij-function", "/usr/bin/cat"}});
  ASSERT_EQ(task.parameters_size(), 1);
  EXPECT_EQ(task.parameters().at("function"), "/usr/bin/cat");
}

TEST(PopulateParametersFromHeadersTest, HeaderNameMatchingIsCaseInsensitive) {
  task::Task task;
  PopulateParametersFromHeaders(task, {{"X-STRIJ-Function", "/usr/bin/cat"}});
  ASSERT_EQ(task.parameters_size(), 1);
  EXPECT_EQ(task.parameters().at("function"), "/usr/bin/cat");
}

TEST(PopulateParametersFromHeadersTest, NonPrefixedHeadersAreNotForwarded) {
  task::Task task;
  PopulateParametersFromHeaders(task, {{"host", "example.com"}, {"authorization", "Bearer xyz"}});
  EXPECT_EQ(task.parameters_size(), 0);
}

TEST(PopulateParametersFromHeadersTest, MultipleXStrijHeadersAreForwarded) {
  task::Task task;
  PopulateParametersFromHeaders(task, {{"x-strij-function", "/usr/bin/cat"}, {"x-strij-cpu", "4"}});
  ASSERT_EQ(task.parameters_size(), 2);
  EXPECT_EQ(task.parameters().at("function"), "/usr/bin/cat");
  EXPECT_EQ(task.parameters().at("cpu"), "4");
}

class GatewayHttpHandlerTest : public ::testing::Test {
protected:
  ResultReceiverStorage storage_;
};

TEST_F(GatewayHttpHandlerTest, HandleMessageForwardsParametersToNode) {
  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillRepeatedly(::testing::Return());

  io::Connection http_conn(fds[0], dispatcher, &owner,
                           [](io::Connection&) -> io::ProtocolParserPtr {
                             return std::make_unique<io::TrivialParser>();
                           });

  // Node directory with one node that we drive into the connected state by
  // simulating a successful connect completion.
  gateway::ResultReceiverStorage node_storage;
  gateway::NodeDirectory directory(
      dispatcher,
      [](io::Connection&) -> io::ProtocolParserPtr {
        return std::make_unique<io::TrivialParser>();
      },
      node_storage);
  event::Completable* node_completable = nullptr;
  EXPECT_CALL(*dispatcher,
              PrepareConnect(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SaveArg<0>(&node_completable), ::testing::Return()));
  directory.AddNode("127.0.0.1:9090", "127.0.0.1:9090");
  ASSERT_NE(node_completable, nullptr);
  node_completable->HandleCompletion(0, 0, 0);

  gateway::schedulers::RoundRobinScheduler scheduler(directory, node_storage);
  GatewayHttpHandler handler(
      storage_,
      [](io::Connection&) -> std::unique_ptr<ResultReceiver> {
        return std::make_unique<NullReceiver>();
      },
      scheduler);

  std::span<const std::byte> written;
  EXPECT_CALL(*dispatcher,
              PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SaveArg<3>(&written), ::testing::Return()));

  io::HttpRequest request{
      .path = "/tasks/echo", .body = {}, .headers = {{"x-strij-function", "/usr/bin/cat"}}};
  handler.HandleMessage(request, http_conn);

  // Reconstruct the task from the written TLV frame.
  std::vector<io::TlvFrame> received_frames;
  io::TlvParser parser(
      [&received_frames](io::TlvFrame frame) { received_frames.push_back(frame); });
  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), written.data(), written.size());
  parser.OnData(written.size());
  ASSERT_EQ(received_frames.size(), 1U);
  EXPECT_EQ(received_frames[0].type_id, io::TlvFrame::kTaskSubmission);

  task::Task task;
  ASSERT_TRUE(task.ParseFromArray(std::bit_cast<const char*>(received_frames[0].value.data()),
                                  static_cast<int>(received_frames[0].value.size())));
  EXPECT_EQ(task.type(), "echo");
  ASSERT_EQ(task.parameters_size(), 1);
  EXPECT_EQ(task.parameters().at("function"), "/usr/bin/cat");
  EXPECT_TRUE(task.has_requirements());
  EXPECT_TRUE(task.requirements().resources().empty());

  close(fds[0]);
  close(fds[1]);
}

TEST_F(GatewayHttpHandlerTest, RoutesTaskThroughScheduler) {
  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillRepeatedly(::testing::Return());

  io::Connection http_conn(fds[0], dispatcher, &owner,
                           [](io::Connection&) -> io::ProtocolParserPtr {
                             return std::make_unique<io::TrivialParser>();
                           });

  gateway::ResultReceiverStorage node_storage2;
  gateway::NodeDirectory directory(
      dispatcher,
      [](io::Connection&) -> io::ProtocolParserPtr {
        return std::make_unique<io::TrivialParser>();
      },
      node_storage2);
  event::Completable* node_completable = nullptr;
  EXPECT_CALL(*dispatcher,
              PrepareConnect(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillRepeatedly(
          ::testing::DoAll(::testing::SaveArg<0>(&node_completable), ::testing::Return()));
  directory.AddNode("A", "10.0.0.1:9090");
  ASSERT_NE(node_completable, nullptr);
  node_completable->HandleCompletion(0, 0, 0);
  directory.AddNode("B", "10.0.0.2:9090");
  node_completable->HandleCompletion(0, 0, 0);

  // The scheduler overrides round-robin and always picks node "B".
  std::vector<task::Task> recorded;
  StubScheduler scheduler(directory.GetNode("B"), &node_storage2, &recorded);
  GatewayHttpHandler handler(
      storage_,
      [](io::Connection&) -> std::unique_ptr<ResultReceiver> {
        return std::make_unique<NullReceiver>();
      },
      scheduler);

  event::Completable* written_to = nullptr;
  std::span<const std::byte> written;
  EXPECT_CALL(*dispatcher,
              PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SaveArg<0>(&written_to),
                                 ::testing::SaveArg<3>(&written), ::testing::Return()));

  io::HttpRequest request{
      .path = "/tasks/echo", .body = {}, .headers = {{"x-strij-resources-cpu", "2"}}};
  handler.HandleMessage(request, http_conn);

  // The task was routed to the node the scheduler selected.
  EXPECT_EQ(written_to, static_cast<event::Completable*>(directory.GetNode("B")->GetConnection()));

  // The scheduler saw exactly one task with the resolved requirements.
  ASSERT_EQ(recorded.size(), 1U);
  EXPECT_EQ(recorded[0].type(), "echo");
  ASSERT_TRUE(recorded[0].has_requirements());
  ASSERT_EQ(recorded[0].requirements().resources().size(), 1U);
  EXPECT_EQ(recorded[0].requirements().resources().at("cpu"), 2U);

  // And the serialized task carried the same type.
  std::vector<io::TlvFrame> received_frames;
  io::TlvParser parser(
      [&received_frames](io::TlvFrame frame) { received_frames.push_back(frame); });
  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), written.data(), written.size());
  parser.OnData(written.size());
  ASSERT_EQ(received_frames.size(), 1U);
  task::Task task;
  ASSERT_TRUE(task.ParseFromArray(std::bit_cast<const char*>(received_frames[0].value.data()),
                                  static_cast<int>(received_frames[0].value.size())));
  EXPECT_EQ(task.type(), "echo");
  ASSERT_TRUE(task.has_requirements());
  ASSERT_EQ(task.requirements().resources().size(), 1U);
  EXPECT_EQ(task.requirements().resources().at("cpu"), 2U);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(GatewayHttpHandlerTest, DeclinedScheduleResolvesReceiverWithoutHttpError) {
  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillRepeatedly(::testing::Return());

  io::Connection http_conn(fds[0], dispatcher, &owner,
                           [](io::Connection&) -> io::ProtocolParserPtr {
                             return std::make_unique<io::TrivialParser>();
                           });

  gateway::ResultReceiverStorage node_storage3;
  gateway::NodeDirectory directory(
      dispatcher,
      [](io::Connection&) -> io::ProtocolParserPtr {
        return std::make_unique<io::TrivialParser>();
      },
      node_storage3);
  event::Completable* node_completable = nullptr;
  EXPECT_CALL(*dispatcher,
              PrepareConnect(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SaveArg<0>(&node_completable), ::testing::Return()));
  directory.AddNode("only", "10.0.0.1:9090");
  ASSERT_NE(node_completable, nullptr);
  node_completable->HandleCompletion(0, 0, 0);

  // The handler defers to the scheduler: a declined schedule (e.g. no node)
  // resolves the receiver through DeliverError instead of the handler writing
  // an HTTP response itself. Nothing is written to the wire by the handler.
  std::vector<task::Task> recorded;
  StubScheduler scheduler(nullptr, nullptr, &recorded);
  auto errors = std::make_shared<std::vector<std::string>>();
  GatewayHttpHandler handler(
      storage_,
      [errors](io::Connection&) -> std::unique_ptr<ResultReceiver> {
        return std::make_unique<MockReceiver>(nullptr, nullptr, errors);
      },
      scheduler);

  EXPECT_CALL(*dispatcher,
              PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .Times(0);

  io::HttpRequest request{.path = "/tasks/echo", .body = {}, .headers = {}};
  handler.HandleMessage(request, http_conn);

  // The scheduler declined and resolved the receiver out-of-band.
  ASSERT_EQ(recorded.size(), 1U);
  EXPECT_EQ(recorded[0].type(), "echo");
  ASSERT_EQ(errors->size(), 1U);
  EXPECT_EQ((*errors)[0], "stub scheduler declined");

  close(fds[0]);
  close(fds[1]);
}

// --- Receiver lifecycle tests ---

TEST_F(GatewayTlvHandlerTest, HttpDropErasesReceiver) {
  ExactStateTracker tracker;
  ResultReceiverStorage storage{&tracker};
  std::vector<std::byte> delivered;
  storage.Put("t1", std::make_unique<MockReceiver>(&delivered), "node-A");
  tracker.RecordSubmission("t1", "node-A", {});
  EXPECT_EQ(tracker.InFlight("node-A"), 1U);

  // Simulate HTTP client drop by triggering end-of-stream read.
  std::array<int, 2> fds{};
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()));
  auto dispatcher = std::make_shared<event::MockDispatcher>();
  event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  io::Connection http_conn(fds[0], dispatcher, &owner,
                           [](io::Connection&) -> io::ProtocolParserPtr {
                             return std::make_unique<io::TrivialParser>();
                           });

  // The handler's close callback erases the receiver and records completion.
  bool close_fired = false;
  http_conn.Mailbox()->RegisterOnClose([&storage, &close_fired] {
    close_fired = true;
    storage.NotifyClientDisconnected("t1");
  });

  ASSERT_NE(storage.Get("t1"), nullptr);
  // End-of-stream read (res == 0) closes the connection and fires callbacks.
  http_conn.HandleCompletion(0 /*kRead*/, 0, 0);

  EXPECT_TRUE(close_fired);
  EXPECT_EQ(storage.Get("t1"), nullptr);
  EXPECT_EQ(tracker.InFlight("node-A"), 0U);

  close(fds[1]);
}

TEST(GatewayReceiverLifecycleTest, HttpDropRecordsCompletion) {
  ExactStateTracker tracker;
  ResultReceiverStorage storage{&tracker};

  std::vector<std::byte> delivered;
  storage.Put("t1", std::make_unique<MockReceiver>(&delivered), "node-A");
  tracker.RecordSubmission("t1", "node-A", {});
  EXPECT_EQ(tracker.InFlight("node-A"), 1U);

  // HTTP client drop: receiver erased and tracker completion recorded.
  storage.NotifyClientDisconnected("t1");

  EXPECT_EQ(storage.Get("t1"), nullptr);
  EXPECT_EQ(tracker.InFlight("node-A"), 0U);
}

TEST(GatewayReceiverLifecycleTest, NodeDropDeliversErrorsAndRecordsCompletion) {
  ExactStateTracker tracker;
  ResultReceiverStorage storage{&tracker};

  std::vector<std::byte> delivered_a;
  std::vector<std::byte> delivered_b;
  auto errors_a = std::make_shared<std::vector<std::string>>();
  auto errors_b = std::make_shared<std::vector<std::string>>();
  storage.Put("t1", std::make_unique<MockReceiver>(&delivered_a, nullptr, errors_a), "node-A");
  storage.Put("t2", std::make_unique<MockReceiver>(&delivered_b, nullptr, errors_b), "node-A");
  storage.Put("t3", std::make_unique<MockReceiver>(&delivered_b, nullptr, errors_b), "node-B");

  // Record submissions so tracker has something to complete.
  tracker.RecordSubmission("t1", "node-A", {});
  tracker.RecordSubmission("t2", "node-A", {});
  tracker.RecordSubmission("t3", "node-B", {});

  storage.NotifyNodeDisconnected("node-A");

  // Receivers for node-A are erased with errors delivered.
  EXPECT_EQ(storage.Get("t1"), nullptr);
  EXPECT_EQ(storage.Get("t2"), nullptr);
  EXPECT_THAT(*errors_a, ::testing::ElementsAre("node disconnected"));

  // Tracker accounting for node-A is unwound; node-B is untouched.
  EXPECT_EQ(tracker.InFlight("node-A"), 0U);
  EXPECT_EQ(tracker.InFlight("node-B"), 1U);

  // Receiver for node-B is untouched.
  EXPECT_NE(storage.Get("t3"), nullptr);
}

TEST(GatewayReceiverLifecycleTest, NodeDropNoReceiversIsNoop) {
  ExactStateTracker tracker;
  ResultReceiverStorage storage{&tracker};
  storage.NotifyNodeDisconnected("nonexistent");
  EXPECT_TRUE(storage.Empty());
}

TEST(GatewayReceiverLifecycleTest, IdempotentDoubleErase) {
  ResultReceiverStorage storage;

  std::vector<std::byte> delivered;
  storage.Put("t1", std::make_unique<MockReceiver>(&delivered), "node-A");

  // Erase once — simulates normal completion path.
  storage.Erase("t1");
  EXPECT_EQ(storage.Get("t1"), nullptr);

  // Second erase — simulates close callback firing after result delivered.
  storage.Erase("t1");
  EXPECT_EQ(storage.Get("t1"), nullptr);
}

TEST(GatewayReceiverLifecycleTest, NodeDropDeliversErrorWhileHttpAlive) {
  ExactStateTracker tracker;
  ResultReceiverStorage storage{&tracker};

  std::vector<std::byte> delivered;
  auto errors = std::make_shared<std::vector<std::string>>();
  storage.Put("t1", std::make_unique<MockReceiver>(&delivered, nullptr, errors), "node-A");
  tracker.RecordSubmission("t1", "node-A", {});

  storage.NotifyNodeDisconnected("node-A");

  EXPECT_EQ(storage.Get("t1"), nullptr);
  ASSERT_EQ(errors->size(), 1U);
  EXPECT_EQ(errors->at(0), "node disconnected");
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::gateway
