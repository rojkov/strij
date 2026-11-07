#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "test/e2e/support/cluster_spec.hh"
#include "test/e2e/support/process.hh"

#include "common/core/config/config_loader.hh"
#include "gateway/config/gateway.pb.h"
#include "gateway/extensions/event_planes/summary_mesh/summary_mesh.pb.h"
#include "gateway/extensions/node_discovery/static/static_node_discovery.pb.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "nodeagent/config/nodeagent.pb.h"
#include "nodeagent/extensions/task_handlers/openworkflow/config.pb.h"
#include "nodeagent/extensions/task_handlers/piped_executable/piped_executable.pb.h"

namespace strij::e2e {
namespace {

// NOLINTBEGIN(modernize-use-trailing-return-type)

auto makeTempDir() -> std::filesystem::path {
  std::filesystem::path base = std::filesystem::temp_directory_path();
  if (const char* test_tmpdir = std::getenv("TEST_TMPDIR");
      test_tmpdir != nullptr && *test_tmpdir != '\0') {
    base = test_tmpdir;
  }

  std::string template_path = (base / "strij-config-generation-XXXXXX").string();
  std::vector<char> buffer(template_path.begin(), template_path.end());
  buffer.push_back('\0');
  char* created = ::mkdtemp(buffer.data());
  return created == nullptr ? std::filesystem::path{} : std::filesystem::path{created};
}

void assignPorts(ClusterSpec& spec, PortAllocator& ports) {
  for (auto& node : spec.nodes) {
    node.port = ports.Allocate();
  }
  for (auto& gateway : spec.gateways) {
    gateway.port = ports.Allocate();
  }
}

TEST(ConfigGenerationTest, DefaultSpecHasTheFanoutTopology) {
  const ClusterSpec spec = DefaultFanoutSpec();
  EXPECT_EQ(spec.nodes.size(), 2U);
  EXPECT_EQ(spec.gateways.size(), 2U);
  EXPECT_EQ(spec.nodes[0].marker, "n0");
  EXPECT_EQ(spec.nodes[1].marker, "n1");
}

TEST(ConfigGenerationTest, GeneratedConfigsRoundTripAndDescribeTheTopology) {
  ASSERT_NE(strij::extensions::task_handlers::piped_executable::PipedExecutableTaskHandlerConfig::
                descriptor(),
            nullptr);
  ClusterSpec spec = DefaultFanoutSpec();
  PortAllocator ports;
  assignPorts(spec, ports);

  const auto directory = makeTempDir();
  ASSERT_FALSE(directory.empty());

  auto generated = GenerateClusterConfiguration(spec, directory);
  ASSERT_TRUE(generated.ok()) << generated.status().message();
  ASSERT_EQ(generated->nodes.size(), spec.nodes.size());
  ASSERT_EQ(generated->gateways.size(), spec.gateways.size());

  for (std::size_t gateway_index = 0; gateway_index < generated->gateways.size(); ++gateway_index) {
    const auto path = generated->gateways[gateway_index].config_path;
    auto loaded = strij::config::LoadConfig<strij::config::GatewayConfig>(path.string());
    ASSERT_TRUE(loaded.ok()) << path << ": " << loaded.status().message();

    EXPECT_EQ(loaded->http_listener().port(), spec.gateways[gateway_index].port);
    ASSERT_TRUE(loaded->has_node_discovery());
    EXPECT_EQ(loaded->node_discovery().name(), "static");

    strij::config::StaticNodeDiscoveryConfig discovery;
    ASSERT_TRUE(loaded->node_discovery().typed_config().UnpackTo(&discovery))
        << loaded->node_discovery().typed_config().type_url();
    ASSERT_EQ(static_cast<std::size_t>(discovery.addresses_size()), spec.nodes.size());
    for (std::size_t node_index = 0; node_index < spec.nodes.size(); ++node_index) {
      EXPECT_EQ(discovery.addresses(static_cast<int>(node_index)),
                "127.0.0.1:" + std::to_string(spec.nodes[node_index].port));
    }

    ASSERT_EQ(loaded->schedulers_size(), 1);
    EXPECT_EQ(loaded->schedulers(0).extension().name(), "round_robin");
  }

  for (std::size_t node_index = 0; node_index < generated->nodes.size(); ++node_index) {
    const auto path = generated->nodes[node_index].config_path;
    auto loaded = strij::config::LoadConfig<strij::config::NodeAgentConfig>(path.string());
    ASSERT_TRUE(loaded.ok()) << path << ": " << loaded.status().message();

    EXPECT_EQ(loaded->tlv_listener().port(), spec.nodes[node_index].port);
    ASSERT_EQ(loaded->pools_size(), 1);
    EXPECT_EQ(loaded->pools(0).name(), spec.nodes[node_index].pool_name);
    EXPECT_EQ(loaded->pools(0).total(), spec.nodes[node_index].pool_total);
    EXPECT_EQ(loaded->schedulers_size(), 2);
    ASSERT_EQ(loaded->task_handlers_size(), 1);
    EXPECT_EQ(loaded->task_handlers(0).name(), "piped_executable");

    EXPECT_TRUE(std::filesystem::exists(generated->nodes[node_index].marker_path));
  }
}

TEST(ConfigGenerationTest, MissingPoolIsGeneratedAsWritten) {
  ASSERT_NE(strij::extensions::task_handlers::piped_executable::PipedExecutableTaskHandlerConfig::
                descriptor(),
            nullptr);
  ClusterSpec spec = DefaultFanoutSpec();
  spec.nodes[0].include_pool = false;
  PortAllocator ports;
  assignPorts(spec, ports);

  const auto directory = makeTempDir();
  ASSERT_FALSE(directory.empty());

  auto generated = GenerateClusterConfiguration(spec, directory);
  ASSERT_TRUE(generated.ok()) << generated.status().message();

  const auto path = generated->nodes[0].config_path;
  auto loaded = strij::config::LoadConfig<strij::config::NodeAgentConfig>(path.string());
  ASSERT_TRUE(loaded.ok()) << path << ": " << loaded.status().message();
  EXPECT_TRUE(loaded->pools().empty());

  std::ifstream input(path);
  ASSERT_TRUE(input);
  std::ostringstream contents;
  contents << input.rdbuf();
  EXPECT_EQ(contents.str().find("pools:"), std::string::npos)
      << "the generator must not silently repair a specification that omits its pool";
}

TEST(ConfigGenerationTest, CrossNodeEventTopologyIsGenerated) {
  ASSERT_NE(
      strij::extensions::task_handlers::openworkflow::OpenWorkflowTaskHandlerConfig::descriptor(),
      nullptr);
  ClusterSpec spec = MakeCrossNodeEventSpec();
  PortAllocator ports;
  assignPorts(spec, ports);

  const auto directory = makeTempDir();
  ASSERT_FALSE(directory.empty());

  auto generated = GenerateClusterConfiguration(spec, directory);
  ASSERT_TRUE(generated.ok()) << generated.status().message();
  ASSERT_EQ(generated->nodes.size(), 2U);
  ASSERT_EQ(generated->gateways.size(), 2U);

  for (std::size_t node_index = 0; node_index < generated->nodes.size(); ++node_index) {
    const auto path = generated->nodes[node_index].config_path;
    auto loaded = strij::config::LoadConfig<strij::config::NodeAgentConfig>(path.string());
    ASSERT_TRUE(loaded.ok()) << path << ": " << loaded.status().message();

    ASSERT_EQ(loaded->pools_size(), 1);
    EXPECT_EQ(loaded->pools(0).name(), spec.nodes[node_index].pool_name);
    EXPECT_EQ(loaded->pools(0).total(), 16U);

    ASSERT_EQ(loaded->task_handlers_size(), 2);
    EXPECT_EQ(loaded->task_handlers(0).name(), "piped_executable");
    EXPECT_EQ(loaded->task_handlers(1).name(), "openworkflow");

    ASSERT_EQ(loaded->schedulers_size(), 2);
    EXPECT_EQ(loaded->schedulers(0).extension().name(), "push");
    EXPECT_EQ(loaded->schedulers(1).extension().name(), "default");
    EXPECT_TRUE(loaded->schedulers(1).local_default());
  }

  for (std::size_t gateway_index = 0; gateway_index < generated->gateways.size(); ++gateway_index) {
    const auto path = generated->gateways[gateway_index].config_path;
    auto loaded = strij::config::LoadConfig<strij::config::GatewayConfig>(path.string());
    ASSERT_TRUE(loaded.ok()) << path << ": " << loaded.status().message();

    ASSERT_EQ(loaded->schedulers_size(), 2);
    EXPECT_EQ(loaded->schedulers(0).extension().name(), "round_robin");
    EXPECT_TRUE(loaded->schedulers(0).task_type().empty());
    EXPECT_EQ(loaded->schedulers(1).extension().name(), "capability_aware");
    EXPECT_EQ(loaded->schedulers(1).task_type(), "openworkflow");

    ASSERT_TRUE(loaded->has_event_plane());
    const auto& event_plane = spec.gateways[gateway_index].event_plane;
    ASSERT_TRUE(event_plane.has_value());
    EXPECT_EQ(loaded->event_plane().extension().name(), "summary_mesh");
    EXPECT_EQ(loaded->event_plane().gateway_id(), event_plane->gateway_id);
    EXPECT_EQ(loaded->event_plane().extension().typed_config().type_url(),
              event_plane->typed_config_type_url);
  }
}

TEST(ConfigGenerationTest, EventPlaneConfigurationRoundTrips) {
  ClusterSpec spec = DefaultFanoutSpec();
  spec.gateways[0].event_plane = MakeSummaryMeshEventPlane("gateway-a", 50, 1000);
  PortAllocator ports;
  assignPorts(spec, ports);

  const auto directory = makeTempDir();
  ASSERT_FALSE(directory.empty());

  auto generated = GenerateClusterConfiguration(spec, directory);
  ASSERT_TRUE(generated.ok()) << generated.status().message();

  const auto path = generated->gateways[0].config_path;
  auto loaded = strij::config::LoadConfig<strij::config::GatewayConfig>(path.string());
  ASSERT_TRUE(loaded.ok()) << path << ": " << loaded.status().message();
  ASSERT_TRUE(loaded->has_event_plane()) << path;
  EXPECT_EQ(loaded->event_plane().extension().name(), "summary_mesh");
  EXPECT_EQ(loaded->event_plane().gateway_id(), "gateway-a");
  EXPECT_EQ(loaded->event_plane().extension().typed_config().type_url(),
            spec.gateways[0].event_plane->typed_config_type_url);

  strij::extensions::event_planes::summary_mesh::SummaryMeshConfig mesh_config;
  ASSERT_TRUE(loaded->event_plane().extension().typed_config().UnpackTo(&mesh_config))
      << loaded->event_plane().extension().typed_config().type_url();
  EXPECT_EQ(mesh_config.refresh_interval_ms(), 50U);
  EXPECT_EQ(mesh_config.peer_ttl_ms(), 1000U);

  const auto second_path = generated->gateways[1].config_path;
  auto second = strij::config::LoadConfig<strij::config::GatewayConfig>(second_path.string());
  ASSERT_TRUE(second.ok()) << second_path << ": " << second.status().message();
  EXPECT_FALSE(second->has_event_plane());
}

TEST(ConfigGenerationTest, NoEventPlaneIsGeneratedWhenUnset) {
  ClusterSpec spec = DefaultFanoutSpec();
  PortAllocator ports;
  assignPorts(spec, ports);

  const auto directory = makeTempDir();
  ASSERT_FALSE(directory.empty());

  auto generated = GenerateClusterConfiguration(spec, directory);
  ASSERT_TRUE(generated.ok()) << generated.status().message();

  const auto path = generated->gateways[0].config_path;
  auto loaded = strij::config::LoadConfig<strij::config::GatewayConfig>(path.string());
  ASSERT_TRUE(loaded.ok()) << path << ": " << loaded.status().message();
  EXPECT_FALSE(loaded->has_event_plane());

  std::ifstream input(path);
  ASSERT_TRUE(input);
  std::ostringstream contents;
  contents << input.rdbuf();
  EXPECT_EQ(contents.str().find("event_plane:"), std::string::npos)
      << "the generator must not materialize an event plane that was not requested";
}

// NOLINTEND(modernize-use-trailing-return-type)

} // namespace
} // namespace strij::e2e
