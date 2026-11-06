#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "test/e2e/support/cluster_spec.hh"
#include "test/e2e/support/process.hh"

#include "absl/status/status.h"

namespace strij::e2e {

/**
 * @brief A per-test cluster of real nodeagent and gateway processes.
 *
 * `Start` is the only supported entry point. It allocates a port immediately
 * before spawning each process, waits for every node listener to accept before
 * any gateway dials (a failed gateway connect is never retried), then proves
 * readiness by observing every node marker through every gateway's HTTP path.
 *
 * `Stop` tears down through the process groups, escalates after a bounded wait,
 * reaps every process, and asserts that no descendant survives and every former
 * listener port refuses connections.
 */
class Cluster {
public:
  explicit Cluster(ClusterSpec spec);
  ~Cluster();

  Cluster(const Cluster&) = delete;
  auto operator=(const Cluster&) -> Cluster& = delete;
  Cluster(Cluster&&) noexcept = delete;
  auto operator=(Cluster&&) noexcept -> Cluster& = delete;

  auto Start() -> absl::Status;
  auto Stop() -> absl::Status;

  [[nodiscard]] auto Started() const -> bool { return started_; }
  [[nodiscard]] auto Escalated() const -> bool;
  [[nodiscard]] auto Spec() const -> const ClusterSpec& { return spec_; }
  [[nodiscard]] auto Nodes() const -> const std::vector<Process>& { return nodes_; }
  [[nodiscard]] auto Gateways() const -> const std::vector<Process>& { return gateways_; }
  [[nodiscard]] auto NodePort(std::size_t index) const -> uint16_t;
  [[nodiscard]] auto GatewayPort(std::size_t index) const -> uint16_t;

  /// Per gateway, the set of node markers observed through its HTTP listener.
  [[nodiscard]] auto ObservedMarkers() const -> const std::vector<std::set<std::string>>& {
    return observed_;
  }

  /// Process names, cached exit states, and log tails. Included in every failure
  /// the harness reports so a test failure is diagnosable without a rerun.
  [[nodiscard]] auto Diagnose() -> std::string;

private:
  auto createRoot() -> absl::Status;
  auto startNodes() -> absl::Status;
  auto startGateways() -> absl::Status;
  auto waitForReadiness() -> absl::Status;
  auto failAndStop(absl::Status status) -> absl::Status;
  auto stopProcesses(std::chrono::milliseconds grace) -> absl::Status;

  ClusterSpec spec_;
  std::filesystem::path root_;
  PortAllocator ports_;
  std::vector<Process> nodes_;
  std::vector<Process> gateways_;
  std::vector<uint16_t> node_ports_;
  std::vector<uint16_t> gateway_ports_;
  std::vector<std::set<std::string>> observed_;
  bool started_{false};
  bool stopped_{false};
};

} // namespace strij::e2e
