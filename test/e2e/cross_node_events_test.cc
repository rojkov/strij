#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "test/e2e/support/cluster.hh"
#include "test/e2e/support/cluster_spec.hh"
#include "test/e2e/support/environment.hh"
#include "test/e2e/support/http_client.hh"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace strij::e2e {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

constexpr std::string_view kOpenWorkflowTaskType = "openworkflow";
constexpr std::string_view kFunctionHeader = "x-strij-function";
constexpr std::string_view kResourcesListenHeader = "x-strij-resources.listen";
constexpr std::string_view kResourcesEmitHeader = "x-strij-resources.emit";

constexpr auto kListenerTimeout = std::chrono::seconds{10};
constexpr auto kEmitterTimeout = std::chrono::seconds{5};
constexpr auto kRetryDeadline = std::chrono::seconds{8};
constexpr auto kRetryInterval = std::chrono::milliseconds{25};

const std::string kListenerExpectedBody =
    R"({"consumer":"n0","event":{"orderId":7,"producer":"n1"}})";
const std::string kEmitterExpectedBody = R"({"node":"n1"})";

auto makeTempDir() -> std::filesystem::path {
  std::filesystem::path base = std::filesystem::temp_directory_path();
  if (const char* test_tmpdir = std::getenv("TEST_TMPDIR");
      test_tmpdir != nullptr && *test_tmpdir != '\0') {
    base = test_tmpdir;
  }

  std::string template_path = (base / "strij-cross-node-events-XXXXXX").string();
  std::vector<char> buffer(template_path.begin(), template_path.end());
  buffer.push_back('\0');
  char* created = ::mkdtemp(buffer.data());
  return created == nullptr ? std::filesystem::path{} : std::filesystem::path{created};
}

auto writeFile(const std::filesystem::path& path, std::string_view content) -> bool {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  output.flush();
  return static_cast<bool>(output);
}

auto listenerDocument() -> std::string {
  return R"YAML(document:
  dsl: '1.0.3'
  namespace: e2e
  name: cross-node-listener
  version: '0.1.0'
do:
  - identify:
      call: strij
      with:
        type: piped_executable
        parameters:
          function: marker.sh
        requirements:
          listen: 1
  - wait:
      listen:
        to:
          one:
            with:
              type: com.example.order.created
      output:
        as: '${ {consumer: $input.node, event: .[0]} }'
)YAML";
}

auto emitterDocument() -> std::string {
  return R"YAML(document:
  dsl: '1.0.3'
  namespace: e2e
  name: cross-node-emitter
  version: '0.1.0'
do:
  - identify:
      call: strij
      with:
        type: piped_executable
        parameters:
          function: marker.sh
        requirements:
          emit: 1
  - warmup:
      emit:
        event:
          with:
            source: https://example.test/e2e
            type: com.example.warmup
            data:
              producer: '${ .node }'
  - publish:
      emit:
        event:
          with:
            source: https://example.test/e2e
            type: com.example.order.created
            data:
              orderId: 7
              producer: '${ .node }'
)YAML";
}

TEST(CrossNodeEventsTest, ListenerOnOneNodeConsumesEmitterOnAnother) {
  RequireIoUring();
  if (testing::Test::IsSkipped()) {
    return;
  }

  const auto directory = makeTempDir();
  ASSERT_FALSE(directory.empty());
  const auto listener_path = directory / "listener.yaml";
  const auto emitter_path = directory / "emitter.yaml";
  ASSERT_TRUE(writeFile(listener_path, listenerDocument())) << listener_path;
  ASSERT_TRUE(writeFile(emitter_path, emitterDocument())) << emitter_path;

  Cluster cluster(MakeCrossNodeEventSpec());
  const auto start = cluster.Start();
  ASSERT_TRUE(start.ok()) << start.message();

  const auto listener_port = cluster.GatewayPort(0);
  const auto emitter_port = cluster.GatewayPort(1);

  // Gateway selection here is not chosen by the HTTP port a workflow is
  // submitted to: node A's subscribe and node B's two publishes each consume
  // the next `GatewayConnectionPool::NextEventCapable` slot. `Cluster::Start`
  // fixes the connection order to gateway start order, so the listener
  // subscribes through gateway 0, the warm-up consumes gateway 0, and the real
  // event goes to gateway 1. Revisit this test if that selection policy changes.
  auto listener = std::async(std::launch::async, [listener_port, listener_path]() {
    return Post(listener_port, absl::StrCat("/tasks/", kOpenWorkflowTaskType), "",
                {{std::string(kFunctionHeader), listener_path.string()},
                 {std::string(kResourcesListenHeader), "1"}},
                kListenerTimeout);
  });

  // Give the listener workflow a head start so the first emitter attempt is
  // likely to be consumed; retries below, not this sleep, provide correctness.
  std::this_thread::sleep_for(std::chrono::milliseconds{100});

  std::string failure;
  std::string emitter_body;
  bool emitter_body_mismatch = false;
  int last_emitter_status = 0;
  std::size_t attempts = 0;
  const auto deadline = std::chrono::steady_clock::now() + kRetryDeadline;

  while (std::chrono::steady_clock::now() < deadline) {
    if (listener.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready) {
      break;
    }

    auto emitter = Post(emitter_port, absl::StrCat("/tasks/", kOpenWorkflowTaskType), "",
                        {{std::string(kFunctionHeader), emitter_path.string()},
                         {std::string(kResourcesEmitHeader), "1"}},
                        kEmitterTimeout);
    ++attempts;
    if (!emitter.ok()) {
      failure = absl::StrCat("emitter submission failed: ", emitter.status().message());
      break;
    }
    last_emitter_status = emitter->status_code;
    if (emitter->status_code != 200 && emitter->status_code != 429) {
      failure = absl::StrCat("emitter returned HTTP ", emitter->status_code, ": ", emitter->body);
      break;
    }
    if (emitter->status_code == 200) {
      emitter_body = emitter->body;
      emitter_body_mismatch = emitter_body_mismatch || emitter_body != kEmitterExpectedBody;
    }

    std::this_thread::sleep_for(kRetryInterval);
  }

  auto listener_response = listener.get();
  if (!failure.empty()) {
    ADD_FAILURE() << failure << " after " << attempts << " emitter attempt(s)\n"
                  << cluster.Diagnose();
  }
  EXPECT_FALSE(emitter_body_mismatch) << "emitter response body was '" << emitter_body << "'\n"
                                      << cluster.Diagnose();
  ASSERT_TRUE(listener_response.ok())
      << "listener request failed after " << attempts << " emitter attempt(s) within "
      << std::chrono::duration_cast<std::chrono::milliseconds>(kRetryDeadline).count()
      << " ms (last emitter HTTP " << last_emitter_status
      << "): " << listener_response.status().message() << "\n"
      << cluster.Diagnose();
  EXPECT_EQ(listener_response->status_code, 200) << listener_response->body << "\n"
                                                 << cluster.Diagnose();
  EXPECT_EQ(listener_response->body, kListenerExpectedBody) << cluster.Diagnose();

  const auto stop = cluster.Stop();
  EXPECT_TRUE(stop.ok()) << stop.message();
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::e2e
