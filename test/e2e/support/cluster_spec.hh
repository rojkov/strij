#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"

namespace strij::e2e {

/// One scheduler entry. Gateway entries use `task_type`; node entries use
/// `local_default` instead (an empty node `task_type` is not a default).
struct SchedulerSpec {
  std::string name;
  std::string task_type;
  bool local_default{false};
  std::string typed_config_type_url;
  /// Raw YAML lines placed under `typed_config`, indented by the writer.
  std::string typed_config_yaml;
};

struct TaskHandlerSpec {
  std::string name;
  std::string typed_config_type_url;
  /// Raw YAML lines placed under `typed_config`, indented by the writer.
  std::string typed_config_yaml;
};

struct NodeSpec {
  std::string marker;
  uint16_t port{0};
  std::string listener_address{"127.0.0.1"};
  std::string pool_name{"cpu"};
  uint32_t pool_total{16};
  /// Set false to generate a deliberately invalid node (no pools block) so the
  /// harness can prove it reports the node's startup error instead of repairing
  /// the input.
  bool include_pool{true};
  std::vector<TaskHandlerSpec> task_handlers;
  std::vector<SchedulerSpec> schedulers;
  std::string log_level{"warn"};
};

struct EventPlaneSpec {
  std::string extension_name;
  std::string typed_config_type_url;
  /// Raw YAML lines placed under `typed_config`, indented by the writer.
  std::string typed_config_yaml;
  std::string gateway_id;
};

struct GatewaySpec {
  uint16_t port{0};
  std::string listener_address{"127.0.0.1"};
  std::vector<SchedulerSpec> schedulers;
  std::optional<EventPlaneSpec> event_plane;
  std::string log_level{"warn"};
};

struct ClusterSpec {
  std::vector<NodeSpec> nodes;
  std::vector<GatewaySpec> gateways;
};

/// A node running `piped_executable`, advertising the `push` protocol (what the
/// gateway round-robin scheduler matches on) and using the default local
/// authority. The marker script prints `{"node":"<marker>"}` (and sleeps when
/// the body is `slow`, for capacity tests).
auto MakeNodeSpec(std::string marker) -> NodeSpec;

/// A gateway using the round-robin scheduler for every task type.
auto MakeGatewaySpec() -> GatewaySpec;

/// The bundled best-effort `summary_mesh` event plane. A short refresh interval
/// keeps route-summary re-broadcasts prompt for the cross-node event scenario.
auto MakeSummaryMeshEventPlane(std::string gateway_id, uint32_t refresh_interval_ms = 50,
                               uint32_t peer_ttl_ms = 1000) -> EventPlaneSpec;

/// Two nodes (markers n0, n1) and two gateways: the fan-out baseline.
auto DefaultFanoutSpec() -> ClusterSpec;

/// The cross-node event topology: node `n0` declares a `listen` pool, node
/// `n1` declares an `emit` pool, both run `openworkflow`, and both event-plane
/// gateways route `openworkflow` through `capability_aware`.
auto MakeCrossNodeEventSpec() -> ClusterSpec;

struct GeneratedNode {
  std::filesystem::path directory;
  std::filesystem::path config_path;
  std::filesystem::path marker_path;
  std::string marker;
};

struct GeneratedGateway {
  std::filesystem::path directory;
  std::filesystem::path config_path;
};

struct GeneratedCluster {
  std::filesystem::path root;
  std::vector<GeneratedNode> nodes;
  std::vector<GeneratedGateway> gateways;
};

/// Writes `nodeagent.yaml` and an executable `marker.sh` into `directory`.
auto WriteNodeConfiguration(const NodeSpec& node, const std::filesystem::path& directory)
    -> absl::StatusOr<GeneratedNode>;

/// Writes `gateway.yaml` into `directory`, listing every node address.
auto WriteGatewayConfiguration(const GatewaySpec& gateway, const std::vector<NodeSpec>& nodes,
                               const std::filesystem::path& directory)
    -> absl::StatusOr<GeneratedGateway>;

/// Writes one directory per process (node<i>/ and gateway<j>/) under `root`.
auto GenerateClusterConfiguration(const ClusterSpec& spec, const std::filesystem::path& root)
    -> absl::StatusOr<GeneratedCluster>;

} // namespace strij::e2e
