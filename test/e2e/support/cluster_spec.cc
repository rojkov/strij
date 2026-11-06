#include "test/e2e/support/cluster_spec.hh"

#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace strij::e2e {

namespace {

constexpr std::string_view kPipedExecutableTypeUrl =
    "type.googleapis.com/strij.extensions.task_handlers.piped_executable."
    "PipedExecutableTaskHandlerConfig";
constexpr std::string_view kStaticDiscoveryTypeUrl =
    "type.googleapis.com/strij.config.StaticNodeDiscoveryConfig";

auto writeFile(const std::filesystem::path& path, std::string_view content) -> absl::Status {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return absl::InternalError(absl::StrCat("failed to open '", path.string(), "' for writing"));
  }
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  output.flush();
  if (!output) {
    return absl::InternalError(absl::StrCat("failed to write '", path.string(), "'"));
  }
  return absl::OkStatus();
}

auto appendSchedulerEntries(std::ostringstream& out, const std::vector<SchedulerSpec>& schedulers,
                            bool node_entries) -> void {
  for (const auto& scheduler : schedulers) {
    out << "  - extension:\n";
    out << "      name: \"" << scheduler.name << "\"\n";
    if (!scheduler.typed_config_type_url.empty() || !scheduler.typed_config_yaml.empty()) {
      out << "      typed_config:\n";
      if (!scheduler.typed_config_type_url.empty()) {
        out << "        \"@type\": \"" << scheduler.typed_config_type_url << "\"\n";
      }
      if (!scheduler.typed_config_yaml.empty()) {
        std::istringstream extra(scheduler.typed_config_yaml);
        std::string line;
        while (std::getline(extra, line)) {
          out << "        " << line << "\n";
        }
      }
    }
    if (node_entries) {
      if (!scheduler.task_type.empty()) {
        out << "    task_type: \"" << scheduler.task_type << "\"\n";
      }
      if (scheduler.local_default) {
        out << "    local_default: true\n";
      }
    } else if (!scheduler.task_type.empty()) {
      out << "    task_type: \"" << scheduler.task_type << "\"\n";
    }
  }
}

auto appendTaskHandlers(std::ostringstream& out, const std::vector<TaskHandlerSpec>& handlers)
    -> void {
  for (const auto& handler : handlers) {
    out << "  - name: \"" << handler.name << "\"\n";
    if (!handler.typed_config_type_url.empty() || !handler.typed_config_yaml.empty()) {
      out << "    typed_config:\n";
      if (!handler.typed_config_type_url.empty()) {
        out << "      \"@type\": \"" << handler.typed_config_type_url << "\"\n";
      }
      if (!handler.typed_config_yaml.empty()) {
        std::istringstream extra(handler.typed_config_yaml);
        std::string line;
        while (std::getline(extra, line)) {
          out << "      " << line << "\n";
        }
      }
    }
  }
}

auto markerScript(std::string_view marker) -> std::string {
  return absl::StrCat("#!/bin/sh\n"
                      "set -eu\n"
                      "input=\"$(cat)\"\n"
                      // A grandchild that outlives the task: cluster teardown must
                      // remove it through the node's process group. Its stdio is
                      // redirected so it cannot hold the task's pipes open.
                      "if [ \"${input}\" = \"spawn-child\" ]; then\n"
                      "  sleep 60 </dev/null >/dev/null 2>&1 &\n"
                      "fi\n"
                      "if [ \"${input}\" = \"slow\" ]; then\n"
                      "  sleep 2\n"
                      "fi\n"
                      "printf '{\"node\":\"%s\"}' \"",
                      marker, "\"\n");
}

} // namespace

auto MakeNodeSpec(std::string marker) -> NodeSpec {
  NodeSpec node;
  node.marker = std::move(marker);
  node.task_handlers.push_back(TaskHandlerSpec{
      .name = "piped_executable",
      .typed_config_type_url = std::string(kPipedExecutableTypeUrl),
      .typed_config_yaml = "capacity:\n  concurrency: 1024",
  });
  node.schedulers.push_back(SchedulerSpec{.name = "push"});
  node.schedulers.push_back(SchedulerSpec{.name = "default", .local_default = true});
  return node;
}

auto MakeGatewaySpec() -> GatewaySpec {
  GatewaySpec gateway;
  gateway.schedulers.push_back(SchedulerSpec{.name = "round_robin"});
  return gateway;
}

auto DefaultFanoutSpec() -> ClusterSpec {
  ClusterSpec spec;
  spec.nodes.push_back(MakeNodeSpec("n0"));
  spec.nodes.push_back(MakeNodeSpec("n1"));
  spec.gateways.push_back(MakeGatewaySpec());
  spec.gateways.push_back(MakeGatewaySpec());
  return spec;
}

auto WriteNodeConfiguration(const NodeSpec& node, const std::filesystem::path& directory)
    -> absl::StatusOr<GeneratedNode> {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("failed to create '", directory.string(), "': ", error.message()));
  }

  std::ostringstream out;
  out << "tlv_listener:\n";
  out << "  address: \"" << node.listener_address << "\"\n";
  out << "  port: " << node.port << "\n\n";

  out << "schedulers:\n";
  appendSchedulerEntries(out, node.schedulers, true);
  out << "\n";

  if (node.include_pool) {
    out << "pools:\n";
    out << "  - name: \"" << node.pool_name << "\"\n";
    out << "    total: " << node.pool_total << "\n\n";
  }

  if (!node.task_handlers.empty()) {
    out << "task_handlers:\n";
    appendTaskHandlers(out, node.task_handlers);
    out << "\n";
  }

  out << "heartbeat_interval:\n";
  out << "  seconds: 5\n\n";
  out << "logging:\n";
  out << "  level: \"" << node.log_level << "\"\n";
  out << "  format: \"text\"\n";
  out << "  output: \"stderr\"\n";

  const auto config_path = directory / "nodeagent.yaml";
  auto status = writeFile(config_path, out.str());
  if (!status.ok()) {
    return status;
  }

  const auto marker_path = directory / "marker.sh";
  status = writeFile(marker_path, markerScript(node.marker));
  if (!status.ok()) {
    return status;
  }
  std::filesystem::permissions(marker_path,
                               std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::group_exec |
                                   std::filesystem::perms::others_exec,
                               std::filesystem::perm_options::add, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("failed to mark '", marker_path.string(), "' executable: ", error.message()));
  }

  return GeneratedNode{
      .directory = directory,
      .config_path = config_path,
      .marker_path = marker_path,
      .marker = node.marker,
  };
}

auto WriteGatewayConfiguration(const GatewaySpec& gateway, const std::vector<NodeSpec>& nodes,
                               const std::filesystem::path& directory)
    -> absl::StatusOr<GeneratedGateway> {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("failed to create '", directory.string(), "': ", error.message()));
  }

  std::ostringstream out;
  out << "http_listener:\n";
  out << "  address: \"" << gateway.listener_address << "\"\n";
  out << "  port: " << gateway.port << "\n\n";

  out << "node_discovery:\n";
  out << "  name: \"static\"\n";
  out << "  typed_config:\n";
  out << "    \"@type\": \"" << kStaticDiscoveryTypeUrl << "\"\n";
  out << "    addresses:\n";
  for (const auto& node : nodes) {
    out << "      - \"" << node.listener_address << ":" << node.port << "\"\n";
  }
  out << "\n";

  out << "schedulers:\n";
  appendSchedulerEntries(out, gateway.schedulers, false);
  out << "\n";

  out << "logging:\n";
  out << "  level: \"" << gateway.log_level << "\"\n";
  out << "  format: \"text\"\n";
  out << "  output: \"stderr\"\n";

  const auto config_path = directory / "gateway.yaml";
  auto status = writeFile(config_path, out.str());
  if (!status.ok()) {
    return status;
  }

  return GeneratedGateway{.directory = directory, .config_path = config_path};
}

auto GenerateClusterConfiguration(const ClusterSpec& spec, const std::filesystem::path& root)
    -> absl::StatusOr<GeneratedCluster> {
  std::error_code error;
  std::filesystem::create_directories(root, error);
  if (error) {
    return absl::InternalError(
        absl::StrCat("failed to create '", root.string(), "': ", error.message()));
  }

  GeneratedCluster generated;
  generated.root = root;
  generated.nodes.reserve(spec.nodes.size());
  generated.gateways.reserve(spec.gateways.size());

  for (std::size_t index = 0; index < spec.nodes.size(); ++index) {
    auto node = WriteNodeConfiguration(spec.nodes[index], root / absl::StrCat("node", index));
    if (!node.ok()) {
      return node.status();
    }
    generated.nodes.push_back(std::move(*node));
  }

  for (std::size_t index = 0; index < spec.gateways.size(); ++index) {
    auto gateway = WriteGatewayConfiguration(spec.gateways[index], spec.nodes,
                                             root / absl::StrCat("gateway", index));
    if (!gateway.ok()) {
      return gateway.status();
    }
    generated.gateways.push_back(std::move(*gateway));
  }

  return generated;
}

} // namespace strij::e2e
