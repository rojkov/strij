#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "core/gateway/gateway_http_handler.hh"
#include "core/gateway/gateway_tlv_handler.hh"
#include "core/gateway/http_result_receiver.hh"
#include "core/gateway/result_receiver_storage.hh"
#include "core/io/connection.hh"
#include "core/io/protocol_parser.hh"
#include "core/io/tlv_frame.hh"
#include "core/io/tlv_parser.hh"
#include "core/task/task.pb.h"
#include "gtest/gtest.h"

namespace strij::gateway {
namespace {

class MockReceiver : public ResultReceiver {
public:
  explicit MockReceiver(std::vector<std::byte>* out,
                        std::shared_ptr<std::vector<bool>> finalities = {})
      : out_{out}, finalities_{std::move(finalities)} {}

  void Deliver(std::span<const std::byte> value, bool is_final) override {
    out_->assign(value.begin(), value.end());
    if (finalities_) {
      finalities_->push_back(is_final);
    }
  }

private:
  std::vector<std::byte>* out_;
  std::shared_ptr<std::vector<bool>> finalities_;
};

class NullReceiver : public ResultReceiver {
public:
  void Deliver(std::span<const std::byte> /*value*/, bool /*is_final*/) override {}
};

auto SerializeTaskResult(const strij::task::TaskResult& result) -> std::string {
  std::string serialized;
  result.SerializeToString(&serialized);
  return serialized;
}

auto toTlvFrame(std::string_view serialized) -> strij::io::TlvFrame {
  return {strij::io::TlvFrame::kResult,
          std::as_bytes(std::span(serialized.data(), serialized.size()))};
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
  GatewayTlvHandler handler_{storage_};
};

TEST_F(GatewayTlvHandlerTest, DispatchResultToReceiver) {
  std::vector<std::byte> delivered;
  storage_.put("42", std::make_unique<MockReceiver>(&delivered));

  strij::task::TaskResult result;
  result.set_id("42");
  result.set_body("CCDD");
  std::string serialized;
  result.SerializeToString(&serialized);
  auto wire = strij::io::SerializeTlvFrame(
      strij::io::TlvFrame::kResult,
      std::as_bytes(std::span(serialized.data(), serialized.size())));

  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  strij::io::Connection conn(fds[0], dispatcher, &owner,
                              [](strij::io::Connection&) -> std::unique_ptr<strij::io::ProtocolParser> {
                                return std::make_unique<strij::io::TrivialParser>();
                              });

  // Reconstruct the frame from wire bytes and deliver through the handler
  std::vector<strij::io::TlvFrame> received_frames;
  strij::io::TlvParser parser(
      [&received_frames](strij::io::TlvFrame frame) { received_frames.push_back(frame); });
  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), wire.data(), wire.size());
  parser.OnData(wire.size());
  ASSERT_EQ(received_frames.size(), 1U);

  handler_.HandleFrame(received_frames[0], conn);

  auto expected = std::vector<std::byte>{std::byte{'C'}, std::byte{'C'}, std::byte{'D'},
                                         std::byte{'D'}};
  ASSERT_EQ(delivered.size(), expected.size());
  EXPECT_TRUE(std::equal(delivered.begin(), delivered.end(), expected.begin()));
  EXPECT_EQ(storage_.get("42"), nullptr);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(GatewayTlvHandlerTest, UnknownTaskIdDropsResult) {
  std::vector<std::byte> delivered;
  storage_.put("1", std::make_unique<MockReceiver>(&delivered));

  strij::task::TaskResult result;
  result.set_id("99");
  result.set_body("data");
  std::string serialized;
  result.SerializeToString(&serialized);
  auto wire = strij::io::SerializeTlvFrame(
      strij::io::TlvFrame::kResult,
      std::as_bytes(std::span(serialized.data(), serialized.size())));

  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  strij::io::Connection conn(fds[0], dispatcher, &owner,
                              [](strij::io::Connection&) -> std::unique_ptr<strij::io::ProtocolParser> {
                                return std::make_unique<strij::io::TrivialParser>();
                              });

  std::vector<strij::io::TlvFrame> received_frames;
  strij::io::TlvParser parser(
      [&received_frames](strij::io::TlvFrame frame) { received_frames.push_back(frame); });
  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), wire.data(), wire.size());
  parser.OnData(wire.size());
  ASSERT_EQ(received_frames.size(), 1U);

  handler_.HandleFrame(received_frames[0], conn);

  EXPECT_TRUE(delivered.empty());
  EXPECT_NE(storage_.get("1"), nullptr);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(GatewayTlvHandlerTest, MalformedResultFrameIsDropped) {
  std::vector<std::byte> delivered;
  storage_.put("7", std::make_unique<MockReceiver>(&delivered));

  auto garbage = std::vector<std::byte>{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE},
                                        std::byte{0xEF}};
  auto wire = strij::io::SerializeTlvFrame(strij::io::TlvFrame::kResult, garbage);

  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Return());
  strij::io::Connection conn(fds[0], dispatcher, &owner,
                              [](strij::io::Connection&) -> std::unique_ptr<strij::io::ProtocolParser> {
                                return std::make_unique<strij::io::TrivialParser>();
                              });

  std::vector<strij::io::TlvFrame> received_frames;
  strij::io::TlvParser parser(
      [&received_frames](strij::io::TlvFrame frame) { received_frames.push_back(frame); });
  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), wire.data(), wire.size());
  parser.OnData(wire.size());
  ASSERT_EQ(received_frames.size(), 1U);

  handler_.HandleFrame(received_frames[0], conn);

  EXPECT_TRUE(delivered.empty());
  EXPECT_NE(storage_.get("7"), nullptr);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(GatewayTlvHandlerTest, IntermediateResultKeepsReceiverUntilFinal) {
  std::vector<std::byte> delivered;
  auto finalities = std::make_shared<std::vector<bool>>();
  auto receiver = std::make_unique<MockReceiver>(&delivered, finalities);
  auto* receiver_raw = receiver.get();
  storage_.put("42", std::move(receiver));

  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillRepeatedly(::testing::Return());
  strij::io::Connection conn(fds[0], dispatcher, &owner,
                              [](strij::io::Connection&) -> std::unique_ptr<strij::io::ProtocolParser> {
                                return std::make_unique<strij::io::TrivialParser>();
                              });

  strij::task::TaskResult intermediate;
  intermediate.set_id("42");
  intermediate.set_body("chunk");
  intermediate.set_is_final(false);
  auto intermediate_serialized = SerializeTaskResult(intermediate);
  handler_.HandleFrame(toTlvFrame(intermediate_serialized), conn);
  EXPECT_EQ(storage_.get("42"), receiver_raw);
  ASSERT_EQ(finalities->size(), 1U);
  EXPECT_FALSE((*finalities)[0]);

  strij::task::TaskResult final_result;
  final_result.set_id("42");
  final_result.set_body("tail");
  final_result.set_is_final(true);
  auto final_serialized = SerializeTaskResult(final_result);
  handler_.HandleFrame(toTlvFrame(final_serialized), conn);
  EXPECT_EQ(storage_.get("42"), nullptr);
  ASSERT_EQ(finalities->size(), 2U);
  EXPECT_TRUE((*finalities)[1]);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(GatewayTlvHandlerTest, AbsentIsFinalFieldTreatsResultAsFinal) {
  std::vector<std::byte> delivered;
  auto receiver = std::make_unique<MockReceiver>(&delivered);
  storage_.put("7", std::move(receiver));

  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillRepeatedly(::testing::Return());
  strij::io::Connection conn(fds[0], dispatcher, &owner,
                              [](strij::io::Connection&) -> std::unique_ptr<strij::io::ProtocolParser> {
                                return std::make_unique<strij::io::TrivialParser>();
                              });

  strij::task::TaskResult result;
  result.set_id("7");
  result.set_body("done");
  auto result_serialized = SerializeTaskResult(result);
  handler_.HandleFrame(toTlvFrame(result_serialized), conn);
  EXPECT_EQ(storage_.get("7"), nullptr);

  close(fds[0]);
  close(fds[1]);
}

TEST(HttpResponseFramerTest, SingleShotUsesContentLength) {
  HttpResponseFramer framer;
  std::vector<std::byte> body = {std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};

  auto frames = framer.Next(body, true);

  ASSERT_EQ(frames.size(), 1U);
  std::string response(reinterpret_cast<const char*>(frames[0].data()), frames[0].size());
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
  std::vector<std::byte> a{std::byte{'a'}};
  std::vector<std::byte> b{std::byte{'b'}};
  std::vector<std::byte> c{std::byte{'c'}};

  auto first = framer.Next(a, false);
  ASSERT_EQ(first.size(), 2U);
  std::string header(reinterpret_cast<const char*>(first[0].data()), first[0].size());
  EXPECT_NE(header.find("HTTP/1.1 200 OK"), std::string::npos);
  EXPECT_NE(header.find("Transfer-Encoding: chunked"), std::string::npos);
  EXPECT_EQ(header.find("Content-Length"), std::string::npos);
  std::string chunk1(reinterpret_cast<const char*>(first[1].data()), first[1].size());
  EXPECT_EQ(chunk1, "1\r\na\r\n");

  auto second = framer.Next(b, false);
  ASSERT_EQ(second.size(), 1U);
  std::string chunk2(reinterpret_cast<const char*>(second[0].data()), second[0].size());
  EXPECT_EQ(chunk2, "1\r\nb\r\n");

  auto third = framer.Next(c, true);
  ASSERT_EQ(third.size(), 2U);
  std::string chunk3(reinterpret_cast<const char*>(third[0].data()), third[0].size());
  EXPECT_EQ(chunk3, "1\r\nc\r\n");
  std::string terminal(reinterpret_cast<const char*>(third[1].data()), third[1].size());
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
  std::string chunk(reinterpret_cast<const char*>(final_result[0].data()), final_result[0].size());
  EXPECT_EQ(chunk, "0\r\n\r\n");
  std::string terminal(reinterpret_cast<const char*>(final_result[1].data()), final_result[1].size());
  EXPECT_EQ(terminal, "0\r\n\r\n");
}

TEST(PopulateParametersFromHeadersTest, XStrijHeaderMapsToParameter) {
  strij::task::Task task;
  PopulateParametersFromHeaders(task, {{"x-strij-function", "/usr/bin/cat"}});
  ASSERT_EQ(task.parameters_size(), 1);
  EXPECT_EQ(task.parameters().at("function"), "/usr/bin/cat");
}

TEST(PopulateParametersFromHeadersTest, HeaderNameMatchingIsCaseInsensitive) {
  strij::task::Task task;
  PopulateParametersFromHeaders(task, {{"X-STRIJ-Function", "/usr/bin/cat"}});
  ASSERT_EQ(task.parameters_size(), 1);
  EXPECT_EQ(task.parameters().at("function"), "/usr/bin/cat");
}

TEST(PopulateParametersFromHeadersTest, NonPrefixedHeadersAreNotForwarded) {
  strij::task::Task task;
  PopulateParametersFromHeaders(task, {{"host", "example.com"}, {"authorization", "Bearer xyz"}});
  EXPECT_EQ(task.parameters_size(), 0);
}

TEST(PopulateParametersFromHeadersTest, MultipleXStrijHeadersAreForwarded) {
  strij::task::Task task;
  PopulateParametersFromHeaders(task,
                                {{"x-strij-function", "/usr/bin/cat"}, {"x-strij-cpu", "4"}});
  ASSERT_EQ(task.parameters_size(), 2);
  EXPECT_EQ(task.parameters().at("function"), "/usr/bin/cat");
  EXPECT_EQ(task.parameters().at("cpu"), "4");
}

class GatewayHttpHandlerTest : public ::testing::Test {
protected:
  ResultReceiverStorage storage_;
};

TEST_F(GatewayHttpHandlerTest, HandleMessageForwardsParametersToNode) {
  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  auto dispatcher = std::make_shared<strij::event::MockDispatcher>();
  strij::event::DummyOwner owner;
  EXPECT_CALL(*dispatcher,
              PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillRepeatedly(::testing::Return());

  strij::io::Connection http_conn(
      fds[0], dispatcher, &owner,
      [](strij::io::Connection&) -> std::unique_ptr<strij::io::ProtocolParser> {
        return std::make_unique<strij::io::TrivialParser>();
      });

  // Node directory with one node that we drive into the connected state by
  // simulating a successful connect completion.
  strij::gateway::NodeDirectory directory(
      dispatcher, {"127.0.0.1:9090"},
      [](strij::io::Connection&) -> std::unique_ptr<strij::io::ProtocolParser> {
        return std::make_unique<strij::io::TrivialParser>();
      });
  strij::event::Completable* node_completable = nullptr;
  EXPECT_CALL(*dispatcher,
              PrepareConnect(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SaveArg<0>(&node_completable), ::testing::Return()));
  directory.StartConnectAll();
  ASSERT_NE(node_completable, nullptr);
  node_completable->HandleCompletion(0, 0, 0);

  GatewayHttpHandler handler(directory, storage_,
                             [](strij::io::Connection&) -> std::unique_ptr<ResultReceiver> {
                               return std::make_unique<NullReceiver>();
                             });

  std::span<const std::byte> written;
  EXPECT_CALL(*dispatcher, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_,
                                        ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SaveArg<3>(&written), ::testing::Return()));

  strij::io::HttpRequest request{.path = "/tasks/echo",
                                 .body = {},
                                 .headers = {{"x-strij-function", "/usr/bin/cat"}}};
  handler.HandleMessage(request, http_conn);

  // Reconstruct the task from the written TLV frame.
  std::vector<strij::io::TlvFrame> received_frames;
  strij::io::TlvParser parser(
      [&received_frames](strij::io::TlvFrame frame) { received_frames.push_back(frame); });
  auto read_buf = parser.GetReadBuffer();
  std::memcpy(read_buf.data(), written.data(), written.size());
  parser.OnData(written.size());
  ASSERT_EQ(received_frames.size(), 1U);
  EXPECT_EQ(received_frames[0].type_id, strij::io::TlvFrame::kTaskSubmission);

  strij::task::Task task;
  ASSERT_TRUE(task.ParseFromArray(reinterpret_cast<const char*>(received_frames[0].value.data()),
                                  static_cast<int>(received_frames[0].value.size())));
  EXPECT_EQ(task.type(), "echo");
  ASSERT_EQ(task.parameters_size(), 1);
  EXPECT_EQ(task.parameters().at("function"), "/usr/bin/cat");

  close(fds[0]);
  close(fds[1]);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::gateway
