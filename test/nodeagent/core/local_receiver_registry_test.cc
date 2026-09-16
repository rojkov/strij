#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "common/task/task.pb.h"
#include "nodeagent/core/local_receiver_registry.hh"
#include "nodeagent/core/registry_result_sender.hh"
#include "strij/gateway/result_receiver_storage.hh"
#include "gtest/gtest.h"

namespace strij::nodeagent {
namespace {

// Shared, test-owned outcome log. The receiver is stored by value of a
// unique_ptr and may be destroyed while still registered (a final result or an
// error erases the registry entry, destroying the receiver), so assertions read
// the log through a shared_ptr that outlives the receiver.
struct ReceiverLog {
  bool delivered{false};
  bool is_final{false};
  std::string body;
  std::string error;
};

class RecordingReceiver final : public gateway::ResultReceiver {
public:
  std::shared_ptr<ReceiverLog> log = std::make_shared<ReceiverLog>();

  void Deliver(std::span<const std::byte> value, bool is_final) override {
    log->delivered = true;
    log->is_final = is_final;
    log->body.assign(reinterpret_cast<const char*>(value.data()), value.size());
  }

  void DeliverError(std::string_view reason) override { log->error = std::string(reason); }
};

TEST(LocalReceiverRegistryTest, PutGetEraseRoundTrip) {
  LocalReceiverRegistry registry;
  EXPECT_TRUE(registry.Empty());
  EXPECT_EQ(registry.Size(), 0U);

  auto receiver = std::make_unique<RecordingReceiver>();
  RecordingReceiver* raw = receiver.get();
  registry.Put("t1", std::move(receiver));

  EXPECT_FALSE(registry.Empty());
  EXPECT_EQ(registry.Size(), 1U);
  EXPECT_EQ(registry.Get("t1"), raw);
  EXPECT_EQ(registry.Get("missing"), nullptr);

  // Put overwrites a prior entry for the same id.
  auto replacement = std::make_unique<RecordingReceiver>();
  RecordingReceiver* replacement_raw = replacement.get();
  registry.Put("t1", std::move(replacement));
  EXPECT_EQ(registry.Get("t1"), replacement_raw);
  EXPECT_EQ(registry.Size(), 1U);

  EXPECT_TRUE(registry.Erase("t1"));
  EXPECT_FALSE(registry.Erase("t1"));
  EXPECT_TRUE(registry.Empty());
  EXPECT_EQ(registry.Size(), 0U);
}

TEST(RegistryResultSenderTest, FinalResultDeliversAndErases) {
  LocalReceiverRegistry registry;
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  registry.Put("child-1", std::move(receiver));

  RegistryResultSender sender(registry, "child-1");
  task::TaskResult result;
  result.set_id("child-1");
  result.set_body("hello");
  result.set_is_final(true);
  sender.Send(std::move(result));

  ASSERT_TRUE(log->delivered);
  EXPECT_TRUE(log->is_final);
  EXPECT_EQ(log->body, "hello");
  EXPECT_TRUE(log->error.empty());
  EXPECT_TRUE(registry.Empty());
}

TEST(RegistryResultSenderTest, NonFinalResultKeepsEntry) {
  LocalReceiverRegistry registry;
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  registry.Put("child-1", std::move(receiver));

  RegistryResultSender sender(registry, "child-1");
  task::TaskResult result;
  result.set_id("child-1");
  result.set_body("chunk");
  // Explicit false → non-final; the entry survives for the remaining frames.
  result.set_is_final(false);
  sender.Send(std::move(result));

  ASSERT_TRUE(log->delivered);
  EXPECT_FALSE(log->is_final);
  EXPECT_EQ(log->body, "chunk");
  EXPECT_EQ(registry.Size(), 1U);
  EXPECT_NE(registry.Get("child-1"), nullptr);
}

TEST(RegistryResultSenderTest, AbsentIsFinalFieldMeansFinal) {
  LocalReceiverRegistry registry;
  auto receiver = std::make_unique<RecordingReceiver>();
  auto log = receiver->log;
  registry.Put("child-1", std::move(receiver));

  RegistryResultSender sender(registry, "child-1");
  task::TaskResult result;
  result.set_id("child-1");
  result.set_body("done");
  // is_final unset → final (proto3 consumers treat absence as final).
  sender.Send(std::move(result));

  ASSERT_TRUE(log->delivered);
  EXPECT_TRUE(log->is_final);
  EXPECT_TRUE(registry.Empty());
}

TEST(RegistryResultSenderTest, UnknownTaskIsDropped) {
  LocalReceiverRegistry registry;
  RegistryResultSender sender(registry, "ghost");
  task::TaskResult result;
  result.set_id("ghost");
  result.set_body("x");
  result.set_is_final(true);
  sender.Send(std::move(result));
  EXPECT_TRUE(registry.Empty());
}

TEST(RegistryResultSenderTest, LifecycleHooksRoundTrip) {
  LocalReceiverRegistry registry;
  RegistryResultSender sender(registry, "child-1");
  bool closed = false;
  const std::size_t token =
      sender.RegisterOnClose([&closed]() { closed = true; });
  sender.UnregisterOnClose(token);
  // Hooks never fire on their own (there is no connection to tear down).
  EXPECT_FALSE(closed);
}

} // namespace
} // namespace strij::nodeagent