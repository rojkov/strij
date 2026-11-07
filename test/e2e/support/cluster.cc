#include "test/e2e/support/cluster.hh"

#include <signal.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "test/e2e/support/environment.hh"
#include "test/e2e/support/http_client.hh"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace strij::e2e {

namespace {

constexpr auto kBootTimeout = std::chrono::seconds{5};
constexpr auto kReadinessTimeout = std::chrono::seconds{5};
constexpr auto kRetryInterval = std::chrono::milliseconds{5};
constexpr auto kStopGrace = std::chrono::seconds{2};
constexpr auto kKillTimeout = std::chrono::seconds{5};
constexpr std::string_view kMarkerFunctionHeader = "x-strij-function";
constexpr std::string_view kMarkerFunction = "marker.sh";
constexpr std::string_view kMarkerTaskType = "piped_executable";

auto markerBody(std::string_view marker) -> std::string {
  return absl::StrCat("{\"node\":\"", marker, "\"}");
}

auto signalGroup(const Process& process, int signal) -> bool {
  if (process.pgid <= 0) {
    return false;
  }
  if (::killpg(process.pgid, signal) != 0) {
    return errno == ESRCH;
  }
  return true;
}

} // namespace

Cluster::Cluster(ClusterSpec spec) : spec_(std::move(spec)) {}

Cluster::~Cluster() {
  if (!stopped_) {
    (void)Stop();
  }
}

auto Cluster::Escalated() const -> bool {
  for (const auto& process : nodes_) {
    if (process.escalated) {
      return true;
    }
  }
  for (const auto& process : gateways_) {
    if (process.escalated) {
      return true;
    }
  }
  return false;
}

auto Cluster::NodePort(std::size_t index) const -> uint16_t {
  return index < node_ports_.size() ? node_ports_[index] : 0;
}

auto Cluster::GatewayPort(std::size_t index) const -> uint16_t {
  return index < gateway_ports_.size() ? gateway_ports_[index] : 0;
}

auto Cluster::createRoot() -> absl::Status {
  std::filesystem::path base = std::filesystem::temp_directory_path();
  if (const char* test_tmpdir = std::getenv("TEST_TMPDIR");
      test_tmpdir != nullptr && *test_tmpdir != '\0') {
    base = test_tmpdir;
  }

  std::string template_path = (base / "strij-e2e-XXXXXX").string();
  std::vector<char> buffer(template_path.begin(), template_path.end());
  buffer.push_back('\0');
  char* created = ::mkdtemp(buffer.data());
  if (created == nullptr) {
    return absl::InternalError(
        absl::StrCat("failed to create a cluster temporary directory: ", std::strerror(errno)));
  }
  root_ = created;
  return absl::OkStatus();
}

auto Cluster::Start() -> absl::Status {
  if (started_) {
    return absl::FailedPreconditionError("cluster has already been started");
  }

  auto status = createRoot();
  if (!status.ok()) {
    return status;
  }
  started_ = true;

  status = startNodes();
  if (!status.ok()) {
    return failAndStop(std::move(status));
  }

  status = startGateways();
  if (!status.ok()) {
    return failAndStop(std::move(status));
  }

  return absl::OkStatus();
}

auto Cluster::startNodes() -> absl::Status {
  const std::string executable = RunfilePath("src/nodeagent/exe/nodeagent");
  for (std::size_t index = 0; index < spec_.nodes.size(); ++index) {
    const auto port = ports_.Allocate();
    if (port == 0) {
      return absl::InternalError("failed to allocate a listener port for a node");
    }
    spec_.nodes[index].port = port;
    node_ports_.push_back(port);

    auto generated =
        WriteNodeConfiguration(spec_.nodes[index], root_ / absl::StrCat("node", index));
    if (!generated.ok()) {
      return generated.status();
    }

    const std::string name = absl::StrCat("node", index);
    auto process = Spawn(SpawnOptions{
        .name = name,
        .executable = executable,
        .args = {absl::StrCat("--config_file=", generated->config_path.string())},
        .working_directory = generated->directory.string(),
        .log_path = (root_ / absl::StrCat(name, ".log")).string(),
    });
    if (!process.ok()) {
      return process.status();
    }
    nodes_.push_back(std::move(*process));

    // The listener probe is what makes the two-phase boot invariant explicit:
    // gateways are not spawned until every node accepts.
    auto listening = WaitForPort(port, kBootTimeout, &nodes_.back());
    if (!listening.ok()) {
      return absl::UnavailableError(
          absl::StrCat("node", index, " did not reach a listening state: ", listening.message()));
    }
  }
  return absl::OkStatus();
}

auto Cluster::startGateways() -> absl::Status {
  // Readiness is proven one gateway at a time: a gateway is fully connected to
  // every node before the next gateway is spawned, which fixes each node's
  // gateway-connection order to gateway start order.
  observed_.assign(spec_.gateways.size(), {});
  const std::string executable = RunfilePath("src/gateway/exe/gateway");
  for (std::size_t index = 0; index < spec_.gateways.size(); ++index) {
    const auto port = ports_.Allocate();
    if (port == 0) {
      return absl::InternalError("failed to allocate a listener port for a gateway");
    }
    spec_.gateways[index].port = port;
    gateway_ports_.push_back(port);

    auto generated = WriteGatewayConfiguration(spec_.gateways[index], spec_.nodes,
                                               root_ / absl::StrCat("gateway", index));
    if (!generated.ok()) {
      return generated.status();
    }

    const std::string name = absl::StrCat("gateway", index);
    auto process = Spawn(SpawnOptions{
        .name = name,
        .executable = executable,
        .args = {absl::StrCat("--config_file=", generated->config_path.string())},
        .working_directory = generated->directory.string(),
        .log_path = (root_ / absl::StrCat(name, ".log")).string(),
    });
    if (!process.ok()) {
      return process.status();
    }
    gateways_.push_back(std::move(*process));

    auto ready = waitForGatewayReadiness(index);
    if (!ready.ok()) {
      return ready;
    }
  }
  return absl::OkStatus();
}

auto Cluster::waitForGatewayReadiness(std::size_t gateway_index) -> absl::Status {
  Process& gateway = gateways_[gateway_index];
  const auto deadline = std::chrono::steady_clock::now() + kReadinessTimeout;

  while (observed_[gateway_index].size() < nodes_.size()) {
    if (HasExited(gateway)) {
      return absl::UnavailableError(
          absl::StrCat(gateway.name, " exited during the readiness barrier (",
                       DescribeExit(gateway), "); log tail:\n", LogTail(gateway.log_path, 40)));
    }
    for (auto& node : nodes_) {
      if (HasExited(node)) {
        return absl::UnavailableError(
            absl::StrCat(node.name, " exited during the readiness barrier (", DescribeExit(node),
                         "); log tail:\n", LogTail(node.log_path, 40)));
      }
    }

    if (std::chrono::steady_clock::now() >= deadline) {
      std::vector<std::string> missing;
      for (const auto& node : spec_.nodes) {
        if (!observed_[gateway_index].contains(node.marker)) {
          missing.push_back(node.marker);
        }
      }
      return absl::DeadlineExceededError(
          absl::StrCat(gateway.name, " did not observe node marker(s) ",
                       absl::StrJoin(missing, ", "), " within the readiness deadline"));
    }

    auto response = Post(gateway_ports_[gateway_index], absl::StrCat("/tasks/", kMarkerTaskType),
                         "", {{std::string(kMarkerFunctionHeader), std::string(kMarkerFunction)}},
                         std::chrono::seconds{1});
    if (response.ok()) {
      if (response->status_code == 200) {
        const std::string marker = [&]() -> std::string {
          for (const auto& node : spec_.nodes) {
            if (response->body == markerBody(node.marker)) {
              return node.marker;
            }
          }
          return {};
        }();
        if (marker.empty()) {
          return absl::InternalError(absl::StrCat(gateway.name,
                                                  " returned an unrecognized marker body '",
                                                  response->body, "' while probing readiness"));
        }
        observed_[gateway_index].insert(marker);
        continue;
      }
      // 429 means the gateway has not attached a node yet; retry until the
      // deadline. Any other status is a real failure, not a readiness wait.
      if (response->status_code != 429) {
        return absl::InternalError(absl::StrCat(gateway.name, " returned HTTP ",
                                                response->status_code,
                                                " during the readiness barrier: ", response->body));
      }
    }

    std::this_thread::sleep_for(kRetryInterval);
  }

  return absl::OkStatus();
}

auto Cluster::Stop() -> absl::Status {
  if (stopped_) {
    return absl::OkStatus();
  }
  auto status = stopProcesses(kStopGrace);
  stopped_ = true;
  return status;
}

auto Cluster::stopProcesses(std::chrono::milliseconds grace) -> absl::Status {
  std::vector<Process*> all;
  all.reserve(nodes_.size() + gateways_.size());
  for (auto& process : nodes_) {
    all.push_back(&process);
  }
  for (auto& process : gateways_) {
    all.push_back(&process);
  }

  // Signal every group before waiting, so teardown of a cluster costs one grace
  // interval rather than one per process.
  for (Process* process : all) {
    (void)signalGroup(*process, SIGTERM);
  }

  const auto graceful_deadline = std::chrono::steady_clock::now() + grace;
  for (;;) {
    bool done = true;
    for (Process* process : all) {
      if (!HasExited(*process) || DescendantsRemaining(*process) != 0) {
        done = false;
        break;
      }
    }
    if (done || std::chrono::steady_clock::now() >= graceful_deadline) {
      break;
    }
    std::this_thread::sleep_for(kRetryInterval);
  }

  for (Process* process : all) {
    if (!HasExited(*process) || DescendantsRemaining(*process) != 0) {
      // Only report escalation when the forced signal was actually delivered to
      // an existing group.
      if (process->pgid > 0 && ::killpg(process->pgid, SIGKILL) == 0) {
        process->escalated = true;
      }
    }
  }

  const auto kill_deadline = std::chrono::steady_clock::now() + kKillTimeout;
  for (;;) {
    bool done = true;
    for (Process* process : all) {
      if (!HasExited(*process) || DescendantsRemaining(*process) != 0) {
        done = false;
        break;
      }
    }
    if (done || std::chrono::steady_clock::now() >= kill_deadline) {
      break;
    }
    std::this_thread::sleep_for(kRetryInterval);
  }

  std::vector<std::string> failures;
  for (Process* process : all) {
    if (!HasExited(*process)) {
      failures.push_back(absl::StrCat(process->name, " did not exit after SIGKILL"));
      continue;
    }
    if (!process->escalated) {
      auto exit_status = ExitStatus(*process);
      if (!exit_status.ok()) {
        failures.push_back(
            absl::StrCat(process->name, " did not exit cleanly: ", exit_status.status().message()));
      } else if (*exit_status != 0) {
        failures.push_back(absl::StrCat(process->name, " exited with status ", *exit_status));
      }
    }
    const auto descendants = DescendantsRemaining(*process);
    if (descendants != 0) {
      failures.push_back(
          absl::StrCat(process->name, " left ", descendants, " descendant process(es) running"));
    }
  }

  for (const auto port : node_ports_) {
    if (IsPortAccepting(port)) {
      failures.push_back(absl::StrCat("node listener port ", port, " still accepts connections"));
    }
  }
  for (const auto port : gateway_ports_) {
    if (IsPortAccepting(port)) {
      failures.push_back(
          absl::StrCat("gateway listener port ", port, " still accepts connections"));
    }
  }

  if (failures.empty()) {
    return absl::OkStatus();
  }
  return absl::InternalError(
      absl::StrCat("cluster teardown failed: ", absl::StrJoin(failures, "; "), "\n", Diagnose()));
}

auto Cluster::failAndStop(absl::Status status) -> absl::Status {
  auto stop_status = Stop();
  std::string message(status.message());
  if (!stop_status.ok()) {
    message = absl::StrCat(message, "\nteardown after failure: ", stop_status.message());
  }
  return absl::Status(status.code(), absl::StrCat(message, "\n", Diagnose()));
}

auto Cluster::Diagnose() -> std::string {
  std::string diagnostic;
  if (!root_.empty()) {
    absl::StrAppend(&diagnostic, "cluster root: ", root_.string(), "\n");
  }

  const auto append_process = [&diagnostic](Process& process) {
    (void)HasExited(process);
    absl::StrAppend(&diagnostic, process.name, " [", process.log_path, "]: ", DescribeExit(process),
                    "\n");
    const std::string tail = LogTail(process.log_path, 40);
    if (!tail.empty()) {
      absl::StrAppend(&diagnostic, "--- ", process.name, " log tail ---\n", tail);
    }
  };

  for (auto& process : nodes_) {
    append_process(process);
  }
  for (auto& process : gateways_) {
    append_process(process);
  }
  return diagnostic;
}

} // namespace strij::e2e
