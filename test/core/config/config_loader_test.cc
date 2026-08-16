#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/config/config_loader.hh"
#include "core/config/gateway.pb.h"
#include "core/config/nodeagent.pb.h"
#include "extensions/schedulers/round_robin/round_robin.pb.h"
#include "extensions/task_handlers/echo/echo_task_handler.pb.h"
#include "gtest/gtest.h"

namespace strij::config {
namespace {

std::string CreateTempFile(const std::string& content) {
  auto dir = std::filesystem::temp_directory_path();
  auto path = dir / "strij_config_test_XXXXXX";
  std::string tmpl = path.string();
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  int fd = mkstemp(buf.data());
  if (fd == -1)
    return "";
  std::string result(buf.data());
  write(fd, content.data(), content.size());
  close(fd);
  return result;
}

TEST(ConfigLoaderTest, ValidGatewayYaml) {
  std::string yaml = R"(
http_listener:
  address: "0.0.0.0"
  port: 8081
node_connections:
  - address: "127.0.0.1:9090"
logging:
  level: "info"
  format: "text"
  output: "stdout"
  include_source_location: false
)";
  std::string path = CreateTempFile(yaml);
  ASSERT_FALSE(path.empty());

  auto result = LoadConfig<GatewayConfig>(path);
  ASSERT_TRUE(result.ok()) << result.status().message();
  const auto& config = result.value();
  EXPECT_EQ(config.http_listener().address(), "0.0.0.0");
  EXPECT_EQ(config.http_listener().port(), 8081u);
  ASSERT_GE(config.node_connections_size(), 1);
  EXPECT_EQ(config.node_connections(0).address(), "127.0.0.1:9090");
  EXPECT_EQ(config.logging().level(), "info");
  EXPECT_EQ(config.logging().format(), "text");
  EXPECT_EQ(config.logging().output(), "stdout");
  EXPECT_FALSE(config.logging().include_source_location());

  std::filesystem::remove(path);
}

TEST(ConfigLoaderTest, GatewaySchedulerExtensionYaml) {
  std::string yaml = R"(
http_listener:
  address: "0.0.0.0"
  port: 8081
scheduler:
  name: "round_robin"
  typed_config:
    "@type": "type.googleapis.com/strij.extensions.schedulers.round_robin.RoundRobinSchedulerConfig"
node_connections:
  - address: "127.0.0.1:9090"
logging:
  level: "info"
)";
  std::string path = CreateTempFile(yaml);
  ASSERT_FALSE(path.empty());

  auto result = LoadConfig<GatewayConfig>(path);
  ASSERT_TRUE(result.ok()) << result.status().message();
  const auto& config = result.value();
  ASSERT_TRUE(config.has_scheduler());
  EXPECT_EQ(config.scheduler().name(), "round_robin");
  EXPECT_TRUE(config.scheduler().typed_config().Is<strij::extensions::schedulers::round_robin::
                                                     RoundRobinSchedulerConfig>());

  std::filesystem::remove(path);
}

TEST(ConfigLoaderTest, ValidNodeAgentYaml) {
  std::string yaml = R"(
tlv_listener:
  address: "0.0.0.0"
  port: 9090
logging:
  level: "debug"
  format: "json"
  output: "stderr"
)";
  std::string path = CreateTempFile(yaml);
  ASSERT_FALSE(path.empty());

  auto result = LoadConfig<NodeAgentConfig>(path);
  ASSERT_TRUE(result.ok()) << result.status().message();
  const auto& config = result.value();
  EXPECT_EQ(config.tlv_listener().address(), "0.0.0.0");
  EXPECT_EQ(config.tlv_listener().port(), 9090u);
  EXPECT_EQ(config.logging().level(), "debug");
  EXPECT_EQ(config.logging().format(), "json");
  EXPECT_EQ(config.logging().output(), "stderr");

  std::filesystem::remove(path);
}

TEST(ConfigLoaderTest, NodeAgentCapabilitiesYaml) {
  std::string yaml = R"(
tlv_listener:
  address: "0.0.0.0"
  port: 9090
pools:
  - name: "cpu"
    total: 16
  - name: "gpu.h100"
    total: 2
reservations:
  - task_type: "video-encode"
    pool: "gpu.h100"
    amount: 1
task_handlers:
  - name: "echo"
    typed_config:
      "@type": "type.googleapis.com/strij.extensions.task_handlers.echo.EchoTaskHandlerConfig"
      capacity:
        concurrency: 1024
        default_resources:
          resources:
            cpu: 2
heartbeat_interval:
  seconds: 5
)";
  std::string path = CreateTempFile(yaml);
  ASSERT_FALSE(path.empty());

  auto result = LoadConfig<NodeAgentConfig>(path);
  ASSERT_TRUE(result.ok()) << result.status().message();
  const auto& config = result.value();
  ASSERT_EQ(config.pools_size(), 2);
  EXPECT_EQ(config.pools(0).name(), "cpu");
  EXPECT_EQ(config.pools(0).total(), 16u);
  EXPECT_EQ(config.pools(1).name(), "gpu.h100");
  EXPECT_EQ(config.pools(1).total(), 2u);
  ASSERT_EQ(config.reservations_size(), 1);
  EXPECT_EQ(config.reservations(0).task_type(), "video-encode");
  EXPECT_EQ(config.reservations(0).amount(), 1u);
  ASSERT_EQ(config.task_handlers_size(), 1);
  EXPECT_EQ(config.task_handlers(0).name(), "echo");
  strij::extensions::task_handlers::echo::EchoTaskHandlerConfig handler_config;
  ASSERT_TRUE(config.task_handlers(0).typed_config().UnpackTo(&handler_config));
  EXPECT_EQ(handler_config.capacity().concurrency(), 1024u);
  EXPECT_EQ(handler_config.capacity().default_resources().resources().at("cpu"), 2u);
  EXPECT_EQ(config.heartbeat_interval().seconds(), 5);

  std::filesystem::remove(path);
}

TEST(ConfigLoaderTest, InvalidPortOutOfRange) {
  std::string yaml = R"(
http_listener:
  address: "0.0.0.0"
  port: 99999
)";
  std::string path = CreateTempFile(yaml);
  ASSERT_FALSE(path.empty());

  auto result = LoadConfig<GatewayConfig>(path);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(result.status().message().find("out of range") != std::string::npos ||
              result.status().message().find("port") != std::string::npos);

  std::filesystem::remove(path);
}

TEST(ConfigLoaderTest, InvalidLogLevel) {
  std::string yaml = R"(
logging:
  level: "verbose"
http_listener:
  address: "0.0.0.0"
  port: 8081
)";
  std::string path = CreateTempFile(yaml);
  ASSERT_FALSE(path.empty());

  auto result = LoadConfig<GatewayConfig>(path);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(result.status().message().find("level") != std::string::npos);

  std::filesystem::remove(path);
}

TEST(ConfigLoaderTest, InvalidAddressPattern) {
  std::string yaml = R"(
http_listener:
  address: "0.0.0.0"
  port: 8081
node_connections:
  - address: "not-a-valid-address"
)";
  std::string path = CreateTempFile(yaml);
  ASSERT_FALSE(path.empty());

  auto result = LoadConfig<GatewayConfig>(path);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(result.status().message().find("address") != std::string::npos ||
              result.status().message().find("pattern") != std::string::npos);

  std::filesystem::remove(path);
}

TEST(ConfigLoaderTest, CliOverrides) {
  auto result =
      LoadConfig<GatewayConfig>("", {"http_listener.port=9090", "http_listener.address=10.0.0.1"});
  ASSERT_TRUE(result.ok()) << result.status().message();
  const auto& config = result.value();
  EXPECT_EQ(config.http_listener().port(), 9090u);
  EXPECT_EQ(config.http_listener().address(), "10.0.0.1");
}

TEST(ConfigLoaderTest, EnvOverridesPort) {
  setenv("STRIJ_GATEWAY_HTTP_LISTENER_PORT", "7070", 1);
  setenv("STRIJ_GATEWAY_HTTP_LISTENER_ADDRESS", "10.0.0.1", 1);

  auto result = LoadConfig<GatewayConfig>("");
  ASSERT_TRUE(result.ok()) << result.status().message();
  const auto& config = result.value();
  EXPECT_EQ(config.http_listener().port(), 7070u);
  EXPECT_EQ(config.http_listener().address(), "10.0.0.1");

  unsetenv("STRIJ_GATEWAY_HTTP_LISTENER_PORT");
  unsetenv("STRIJ_GATEWAY_HTTP_LISTENER_ADDRESS");
}

TEST(ConfigLoaderTest, EnvOverridesNodeAgent) {
  setenv("STRIJ_NODEAGENT_TLV_LISTENER_PORT", "8080", 1);
  setenv("STRIJ_NODEAGENT_TLV_LISTENER_ADDRESS", "0.0.0.0", 1);
  setenv("STRIJ_NODEAGENT_LOGGING_LEVEL", "error", 1);

  auto result = LoadConfig<NodeAgentConfig>("");
  ASSERT_TRUE(result.ok()) << result.status().message();
  const auto& config = result.value();
  EXPECT_EQ(config.tlv_listener().port(), 8080u);
  EXPECT_EQ(config.tlv_listener().address(), "0.0.0.0");
  EXPECT_EQ(config.logging().level(), "error");

  unsetenv("STRIJ_NODEAGENT_TLV_LISTENER_PORT");
  unsetenv("STRIJ_NODEAGENT_TLV_LISTENER_ADDRESS");
  unsetenv("STRIJ_NODEAGENT_LOGGING_LEVEL");
}

TEST(ConfigLoaderTest, EnvOverridesArrayField) {
  setenv("STRIJ_GATEWAY_HTTP_LISTENER_ADDRESS", "0.0.0.0", 1);
  setenv("STRIJ_GATEWAY_HTTP_LISTENER_PORT", "8081", 1);
  setenv("STRIJ_GATEWAY_NODE_CONNECTIONS__0__ADDRESS", "10.0.0.1:9090", 1);
  setenv("STRIJ_GATEWAY_NODE_CONNECTIONS__1__ADDRESS", "10.0.0.2:9090", 1);

  auto result = LoadConfig<GatewayConfig>("");
  ASSERT_TRUE(result.ok()) << result.status().message();
  const auto& config = result.value();
  ASSERT_GE(config.node_connections_size(), 2);
  EXPECT_EQ(config.node_connections(0).address(), "10.0.0.1:9090");
  EXPECT_EQ(config.node_connections(1).address(), "10.0.0.2:9090");

  unsetenv("STRIJ_GATEWAY_HTTP_LISTENER_ADDRESS");
  unsetenv("STRIJ_GATEWAY_HTTP_LISTENER_PORT");
  unsetenv("STRIJ_GATEWAY_NODE_CONNECTIONS__0__ADDRESS");
  unsetenv("STRIJ_GATEWAY_NODE_CONNECTIONS__1__ADDRESS");
}

TEST(ConfigLoaderTest, ValidateConfig) {
  GatewayConfig config;
  config.mutable_http_listener()->set_address("0.0.0.0");
  config.mutable_http_listener()->set_port(8081);
  config.mutable_logging()->set_level("info");

  auto status = ValidateConfig(config);
  EXPECT_TRUE(status.ok());
}

TEST(ConfigLoaderTest, ValidateConfigInvalidPort) {
  GatewayConfig config;
  config.mutable_http_listener()->set_address("0.0.0.0");
  config.mutable_http_listener()->set_port(99999);

  auto status = ValidateConfig(config);
  EXPECT_FALSE(status.ok());
}

TEST(ConfigLoaderTest, UnknownFieldWarning) {
  std::string yaml = R"(
http_listener:
  address: "0.0.0.0"
  port: 8081
unknown_field: "test"
)";
  std::string path = CreateTempFile(yaml);
  ASSERT_FALSE(path.empty());

  auto result = LoadConfig<GatewayConfig>(path);
  EXPECT_TRUE(result.ok()) << result.status().message();

  std::filesystem::remove(path);
}

TEST(ConfigLoaderTest, MissingYamlFile) {
  auto result = LoadConfig<GatewayConfig>("/nonexistent/config.yaml");
  EXPECT_FALSE(result.ok());
}

TEST(ConfigLoaderTest, NodeAgentCliOverrides) {
  auto result =
      LoadConfig<NodeAgentConfig>("", {"tlv_listener.port=7070", "tlv_listener.address=0.0.0.0"});
  ASSERT_TRUE(result.ok()) << result.status().message();
  const auto& config = result.value();
  EXPECT_EQ(config.tlv_listener().port(), 7070u);
  EXPECT_EQ(config.tlv_listener().address(), "0.0.0.0");
}

TEST(ConfigLoaderTest, GetDefaultConfig) {
  GatewayConfig config = GetDefaultConfig<GatewayConfig>();
  EXPECT_EQ(config.http_listener().port(), 0u);
}

} // namespace
} // namespace strij::config
