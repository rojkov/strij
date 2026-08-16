#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "test/mocks/common/common_mocks.hh"
#include "test/mocks/event/mocks.hh"

#include "core/io/connection.hh"
#include "core/io/protocol_parser.hh"
#include "core/io/tlv_frame.hh"
#include "core/node/capabilities.pb.h"
#include "core/nodeagent/admission_controller.hh"
#include "core/nodeagent/nodeagent_tlv_handler.hh"
#include "core/nodeagent/task_handler_manager.hh"
#include "core/task/task.pb.h"
#include "extensions/task_handlers/echo/echo_task_handler.hh"
#include "gtest/gtest.h"

namespace strij::nodeagent {
namespace {

class RetainingSenderHandler final : public extensions::TaskHandler {
public:
  void HandleTask(const task::Task& task,
                  std::unique_ptr<extensions::ResultSender> sender) override {
    task_id_ = task.id();
    // Own the sender past HandleTask (as an async handler would).
    retained_ = std::move(sender);
  }

  void SendResults() {
    task::TaskResult first;
    first.set_id(task_id_);
    first.set_body("chunk1");
    retained_->Send(std::move(first));

    task::TaskResult last;
    last.set_id(task_id_);
    last.set_body("chunk2");
    last.set_is_final(true);
    retained_->Send(std::move(last));
  }

private:
  std::string task_id_;
  std::unique_ptr<extensions::ResultSender> retained_;
};

class NodeagentTlvHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds_));
    dispatcher_ = std::make_shared<event::MockDispatcher>();
    // Suppress the PrepareRead issued by the Connection constructor.
    EXPECT_CALL(*dispatcher_,
                PrepareRead(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return());
    conn_ = std::make_unique<io::Connection>(fds_[0], dispatcher_, &owner_,
                                             [](io::Connection&) -> io::ProtocolParserPtr {
                                               return std::make_unique<io::TrivialParser>();
                                             });
  }

  void TearDown() override {
    conn_.reset();
    close(fds_[0]);
    close(fds_[1]);
  }

  auto MakeEchoManager() -> std::shared_ptr<TaskHandlerManager> {
    auto manager = std::make_shared<TaskHandlerManager>();
    manager->AddHandler("echo", std::make_unique<extensions::task_handlers::EchoTaskHandler>());
    return manager;
  }

  auto MakeCapabilities() -> std::shared_ptr<const node::NodeCapabilities> {
    auto caps = std::make_shared<node::NodeCapabilities>();
    caps->set_node_id("node-test");
    caps->set_capability_version(1);
    return caps;
  }

  auto MakeAdmission() -> std::shared_ptr<AdmissionController> {
    return std::make_shared<AdmissionController>(*MakeCapabilities());
  }

  // Reads up to `size` bytes written by the handler into `buf`.
  // Returns the number of bytes read, or -1 if nothing was written.
  auto ReadWritten(std::span<std::byte> buf) -> ssize_t {
    return ::recv(fds_[1], buf.data(), buf.size(), MSG_DONTWAIT);
  }

  int fds_[2];
  std::shared_ptr<event::MockDispatcher> dispatcher_;
  event::DummyOwner owner_;
  io::ConnectionPtr conn_;
};

// NOLINTBEGIN(modernize-use-trailing-return-type)

TEST_F(NodeagentTlvHandlerTest, EchoesTaskAsTaskResult) {
  task::Task task;
  task.set_id("42");
  task.set_type("echo");
  task.set_body("hello");
  std::string serialized;
  task.SerializeToString(&serialized);
  auto wire = io::SerializeTlvFrame(io::TlvFrame::kTaskSubmission,
                                    std::as_bytes(std::span(serialized.data(), serialized.size())));

  // When Connection::Write submits, perform the actual write to the socket.
  EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .WillOnce(::testing::Invoke([this](event::Completable*, uint8_t, int,
                                         std::span<const std::byte> buf,
                                         off_t) { ::write(fds_[0], buf.data(), buf.size()); }));

  NodeagentTlvHandler handler(MakeEchoManager(), MakeCapabilities(), MakeAdmission());
  handler.HandleFrame(
      {.type_id = io::TlvFrame::kTaskSubmission, .value = std::as_bytes(std::span(serialized))},
      *conn_);

  std::array<std::byte, 1024> buf{};
  ssize_t n = ReadWritten(buf);
  ASSERT_GT(n, 5);

  EXPECT_EQ(static_cast<uint8_t>(buf[0]), io::TlvFrame::kResult);
  uint32_t net_len{};
  std::memcpy(&net_len, buf.data() + 1, 4);
  uint32_t length = ntohl(net_len);
  EXPECT_EQ(length, static_cast<uint32_t>(n - 5));

  task::TaskResult result;
  ASSERT_TRUE(result.ParseFromArray(buf.data() + 5, static_cast<int>(n - 5)));
  EXPECT_EQ(result.id(), "42");
  EXPECT_EQ(result.body(), "hello");
}

TEST_F(NodeagentTlvHandlerTest, DropsMalformedTask) {
  auto garbage =
      std::vector<std::byte>{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
  auto wire = io::SerializeTlvFrame(io::TlvFrame::kTaskSubmission, garbage);

  EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .Times(0);

  NodeagentTlvHandler handler(std::make_shared<TaskHandlerManager>(), MakeCapabilities(),
                              MakeAdmission());
  handler.HandleFrame({.type_id = io::TlvFrame::kTaskSubmission, .value = garbage}, *conn_);

  std::array<std::byte, 16> buf{};
  EXPECT_EQ(ReadWritten(buf), -1);
  // errno should be EAGAIN/EWOULDBLOCK when nothing was written.
  EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
}

TEST_F(NodeagentTlvHandlerTest, DropsTaskWithNoRegisteredHandler) {
  task::Task task;
  task.set_id("7");
  task.set_type("unknown");
  task.set_body("x");
  std::string serialized;
  task.SerializeToString(&serialized);
  auto wire = io::SerializeTlvFrame(io::TlvFrame::kTaskSubmission,
                                    std::as_bytes(std::span(serialized.data(), serialized.size())));

  EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .Times(0);

  NodeagentTlvHandler handler(MakeEchoManager(), MakeCapabilities(), MakeAdmission());
  handler.HandleFrame(
      {.type_id = io::TlvFrame::kTaskSubmission, .value = std::as_bytes(std::span(serialized))},
      *conn_);

  std::array<std::byte, 16> buf{};
  EXPECT_EQ(ReadWritten(buf), -1);
}

TEST_F(NodeagentTlvHandlerTest, AsyncHandlerRetainsSenderAndSendsTwice) {
  task::Task task;
  task.set_id("99");
  task.set_type("retaining");
  task.set_body("hello");
  std::string serialized;
  task.SerializeToString(&serialized);
  auto wire = io::SerializeTlvFrame(io::TlvFrame::kTaskSubmission,
                                    std::as_bytes(std::span(serialized.data(), serialized.size())));

  auto manager = std::make_shared<TaskHandlerManager>();
  auto handler = std::make_unique<RetainingSenderHandler>();
  auto* raw = handler.get();
  manager->AddHandler("retaining", std::move(handler));

  // When Connection::Write submits, perform the write to the socket and
  // complete it immediately so the next queued buffer drains.
  EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .WillRepeatedly(::testing::Invoke([this](event::Completable* io, uint8_t tag, int fd,
                                               std::span<const std::byte> buf, off_t) {
        ::write(fd, buf.data(), buf.size());
        conn_->HandleCompletion(tag, static_cast<int>(buf.size()), 0);
      }));

  NodeagentTlvHandler handler_wrapper(manager, MakeCapabilities(), MakeAdmission());
  handler_wrapper.HandleFrame(
      {.type_id = io::TlvFrame::kTaskSubmission, .value = std::as_bytes(std::span(serialized))},
      *conn_);

  // The handler retained its sender: it may send results after HandleFrame
  // returned.
  raw->SendResults();

  std::array<std::byte, 2048> buf{};
  ssize_t n = ReadWritten(buf);
  ASSERT_GT(n, 0);

  auto parse_at = [&buf](size_t offset) -> std::pair<task::TaskResult, size_t> {
    EXPECT_EQ(static_cast<uint8_t>(buf[offset]), io::TlvFrame::kResult);
    uint32_t net_len{};
    std::memcpy(&net_len, buf.data() + offset + 1, 4);
    uint32_t length = ntohl(net_len);
    task::TaskResult result;
    EXPECT_TRUE(result.ParseFromArray(buf.data() + offset + 5, static_cast<int>(length)));
    return {std::move(result), static_cast<size_t>(5 + length)};
  };

  auto [first, first_size] = parse_at(0);
  EXPECT_EQ(first.id(), "99");
  EXPECT_EQ(first.body(), "chunk1");
  EXPECT_FALSE(first.is_final());

  auto [second, second_size] = parse_at(first_size);
  EXPECT_EQ(second.id(), "99");
  EXPECT_EQ(second.body(), "chunk2");
  EXPECT_TRUE(second.is_final());

  EXPECT_EQ(n, static_cast<ssize_t>(first_size + second_size));
}

TEST_F(NodeagentTlvHandlerTest, SendAdvertisementWritesCapabilitiesAsFirstFrame) {
  // When Connection::Write submits, perform the actual write to the socket.
  EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .WillOnce(::testing::Invoke([this](event::Completable*, uint8_t, int,
                                         std::span<const std::byte> buf,
                                         off_t) { ::write(fds_[0], buf.data(), buf.size()); }));

  auto caps = std::make_shared<node::NodeCapabilities>();
  caps->set_node_id("node-abc");
  caps->set_capability_version(1);
  caps->add_update_channels()->set_kind("heartbeat");

  NodeagentTlvHandler handler(MakeEchoManager(), caps, MakeAdmission());
  handler.SendAdvertisement(*conn_);

  std::array<std::byte, 1024> buf{};
  ssize_t n = ReadWritten(buf);
  ASSERT_GT(n, 5);

  EXPECT_EQ(static_cast<uint8_t>(buf[0]), io::TlvFrame::kNodeAdvertisement);
  uint32_t net_len{};
  std::memcpy(&net_len, buf.data() + 1, 4);
  uint32_t length = ntohl(net_len);
  ASSERT_EQ(length, static_cast<uint32_t>(n - 5));

  node::NodeCapabilities parsed;
  ASSERT_TRUE(parsed.ParseFromArray(buf.data() + 5, static_cast<int>(n - 5)));
  EXPECT_EQ(parsed.node_id(), "node-abc");
  EXPECT_EQ(parsed.update_channels_size(), 1);
}

TEST_F(NodeagentTlvHandlerTest, RejectsTaskWhenPoolExhausted) {
  auto caps = std::make_shared<node::NodeCapabilities>();
  caps->set_node_id("node-test");
  caps->set_capability_version(1);
  auto* pool = caps->add_pools();
  pool->set_name("gpu.h100");
  pool->set_total(1);
  auto* handler_cap = caps->add_handlers();
  handler_cap->set_task_type("retaining");
  (*handler_cap->mutable_default_resources()->mutable_resources())["gpu.h100"] = 1;

  auto manager = std::make_shared<TaskHandlerManager>();
  manager->AddHandler("retaining", std::make_unique<RetainingSenderHandler>());
  auto admission = std::make_shared<AdmissionController>(*caps);

  // Only the rejected task writes a frame.
  EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .WillOnce(::testing::Invoke([this](event::Completable*, uint8_t, int,
                                         std::span<const std::byte> buf,
                                         off_t) { ::write(fds_[0], buf.data(), buf.size()); }));

  NodeagentTlvHandler handler(manager, caps, admission);

  task::Task first;
  first.set_id("1");
  first.set_type("retaining");
  std::string first_serialized;
  first.SerializeToString(&first_serialized);
  handler.HandleFrame({.type_id = io::TlvFrame::kTaskSubmission,
                       .value = std::as_bytes(std::span(first_serialized))},
                      *conn_);

  task::Task second;
  second.set_id("2");
  second.set_type("retaining");
  std::string second_serialized;
  second.SerializeToString(&second_serialized);
  handler.HandleFrame({.type_id = io::TlvFrame::kTaskSubmission,
                       .value = std::as_bytes(std::span(second_serialized))},
                      *conn_);

  std::array<std::byte, 1024> buf{};
  ssize_t n = ReadWritten(buf);
  ASSERT_GT(n, 5);
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), io::TlvFrame::kTaskRejected);
  uint32_t net_len{};
  std::memcpy(&net_len, buf.data() + 1, 4);
  uint32_t length = ntohl(net_len);
  EXPECT_EQ(length, static_cast<uint32_t>(n - 5));

  task::TaskRejected rejected;
  ASSERT_TRUE(rejected.ParseFromArray(buf.data() + 5, static_cast<int>(n - 5)));
  EXPECT_EQ(rejected.id(), "2");
  EXPECT_NE(rejected.reason().find("gpu.h100"), std::string::npos);
}

TEST_F(NodeagentTlvHandlerTest, RejectsTaskAtConcurrencyLimit) {
  auto caps = std::make_shared<node::NodeCapabilities>();
  caps->set_node_id("node-test");
  caps->set_capability_version(1);
  auto* handler_cap = caps->add_handlers();
  handler_cap->set_task_type("retaining");
  handler_cap->set_concurrency(1);

  auto manager = std::make_shared<TaskHandlerManager>();
  manager->AddHandler("retaining", std::make_unique<RetainingSenderHandler>());
  auto admission = std::make_shared<AdmissionController>(*caps);

  EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .WillOnce(::testing::Invoke([this](event::Completable*, uint8_t, int,
                                         std::span<const std::byte> buf,
                                         off_t) { ::write(fds_[0], buf.data(), buf.size()); }));

  NodeagentTlvHandler handler(manager, caps, admission);

  task::Task first;
  first.set_id("1");
  first.set_type("retaining");
  std::string first_serialized;
  first.SerializeToString(&first_serialized);
  handler.HandleFrame({.type_id = io::TlvFrame::kTaskSubmission,
                       .value = std::as_bytes(std::span(first_serialized))},
                      *conn_);

  task::Task second;
  second.set_id("2");
  second.set_type("retaining");
  std::string second_serialized;
  second.SerializeToString(&second_serialized);
  handler.HandleFrame({.type_id = io::TlvFrame::kTaskSubmission,
                       .value = std::as_bytes(std::span(second_serialized))},
                      *conn_);

  std::array<std::byte, 1024> buf{};
  ssize_t n = ReadWritten(buf);
  ASSERT_GT(n, 5);
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), io::TlvFrame::kTaskRejected);

  task::TaskRejected rejected;
  uint32_t net_len{};
  std::memcpy(&net_len, buf.data() + 1, 4);
  uint32_t length = ntohl(net_len);
  ASSERT_TRUE(rejected.ParseFromArray(buf.data() + 5, static_cast<int>(length)));
  EXPECT_EQ(rejected.id(), "2");
  EXPECT_NE(rejected.reason().find("concurrency limit"), std::string::npos);
}

TEST_F(NodeagentTlvHandlerTest, CompletionReleasesReservedCapacity) {
  auto caps = std::make_shared<node::NodeCapabilities>();
  caps->set_node_id("node-test");
  caps->set_capability_version(1);
  auto* pool = caps->add_pools();
  pool->set_name("cpu");
  pool->set_total(16);
  auto* handler_cap = caps->add_handlers();
  handler_cap->set_task_type("echo");
  (*handler_cap->mutable_default_resources()->mutable_resources())["cpu"] = 2;

  auto admission = std::make_shared<AdmissionController>(*caps);
  EXPECT_EQ(admission->SharedFree("cpu"), 16U);

  EXPECT_CALL(*dispatcher_, PrepareWrite(::testing::_, ::testing::_, ::testing::_, ::testing::_, 0))
      .WillOnce(::testing::Invoke([this](event::Completable*, uint8_t, int,
                                         std::span<const std::byte> buf,
                                         off_t) { ::write(fds_[0], buf.data(), buf.size()); }));

  NodeagentTlvHandler handler(MakeEchoManager(), caps, admission);

  task::Task task;
  task.set_id("1");
  task.set_type("echo");
  std::string serialized;
  task.SerializeToString(&serialized);
  handler.HandleFrame(
      {.type_id = io::TlvFrame::kTaskSubmission, .value = std::as_bytes(std::span(serialized))},
      *conn_);

  // The echo handler sends the final result immediately; the tracking sender
  // releases the reserved capacity.
  EXPECT_EQ(admission->SharedFree("cpu"), 16U);
  EXPECT_EQ(admission->InFlight("echo"), 0U);
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::nodeagent
