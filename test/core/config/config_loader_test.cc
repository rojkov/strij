#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "core/config/config_loader.hh"
#include "gateway.pb.h"
#include "nodeagent.pb.h"
#include "gtest/gtest.h"

namespace carrot::config {
namespace {

std::string CreateTempFile(const std::string& content) {
  auto dir = std::filesystem::temp_directory_path();
  auto path = dir / "carrot_config_test_XXXXXX";
  std::string tmpl = path.string();
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  int fd = mkstemp(buf.data());
  if (fd == -1) return "";
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

  GatewayConfig config;
  ConfigLoadResult result = LoadConfig<GatewayConfig>(path, {}, &config);
  EXPECT_TRUE(result.success) << result.error_message;
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

  NodeAgentConfig config;
  ConfigLoadResult result = LoadConfig<NodeAgentConfig>(path, {}, &config);
  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(config.tlv_listener().address(), "0.0.0.0");
  EXPECT_EQ(config.tlv_listener().port(), 9090u);
  EXPECT_EQ(config.logging().level(), "debug");
  EXPECT_EQ(config.logging().format(), "json");
  EXPECT_EQ(config.logging().output(), "stderr");

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

  GatewayConfig config;
  ConfigLoadResult result = LoadConfig<GatewayConfig>(path, {}, &config);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error_message.find("out of range") != std::string::npos ||
              result.error_message.find("port") != std::string::npos);

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

  GatewayConfig config;
  ConfigLoadResult result = LoadConfig<GatewayConfig>(path, {}, &config);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error_message.find("level") != std::string::npos);

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

  GatewayConfig config;
  ConfigLoadResult result = LoadConfig<GatewayConfig>(path, {}, &config);
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.error_message.find("address") != std::string::npos ||
              result.error_message.find("pattern") != std::string::npos);

  std::filesystem::remove(path);
}

TEST(ConfigLoaderTest, CliOverrides) {
  GatewayConfig config;
  ConfigLoadResult result = LoadConfig<GatewayConfig>(
      "", {"http_listener.port=9090", "http_listener.address=10.0.0.1"}, &config);
  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(config.http_listener().port(), 9090u);
  EXPECT_EQ(config.http_listener().address(), "10.0.0.1");
}

TEST(ConfigLoaderTest, EnvOverridesPort) {
  setenv("CARROT_GATEWAY_HTTP_LISTENER_PORT", "7070", 1);
  setenv("CARROT_GATEWAY_HTTP_LISTENER_ADDRESS", "10.0.0.1", 1);

  GatewayConfig config;
  ConfigLoadResult result = LoadConfig<GatewayConfig>("", {}, &config);
  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(config.http_listener().port(), 7070u);
  EXPECT_EQ(config.http_listener().address(), "10.0.0.1");

  unsetenv("CARROT_GATEWAY_HTTP_LISTENER_PORT");
  unsetenv("CARROT_GATEWAY_HTTP_LISTENER_ADDRESS");
}

TEST(ConfigLoaderTest, EnvOverridesNodeAgent) {
  setenv("CARROT_NODEAGENT_TLV_LISTENER_PORT", "8080", 1);
  setenv("CARROT_NODEAGENT_TLV_LISTENER_ADDRESS", "0.0.0.0", 1);
  setenv("CARROT_NODEAGENT_LOGGING_LEVEL", "error", 1);

  NodeAgentConfig config;
  ConfigLoadResult result = LoadConfig<NodeAgentConfig>("", {}, &config);
  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(config.tlv_listener().port(), 8080u);
  EXPECT_EQ(config.tlv_listener().address(), "0.0.0.0");
  EXPECT_EQ(config.logging().level(), "error");

  unsetenv("CARROT_NODEAGENT_TLV_LISTENER_PORT");
  unsetenv("CARROT_NODEAGENT_TLV_LISTENER_ADDRESS");
  unsetenv("CARROT_NODEAGENT_LOGGING_LEVEL");
}

TEST(ConfigLoaderTest, EnvOverridesArrayField) {
  setenv("CARROT_GATEWAY_HTTP_LISTENER_ADDRESS", "0.0.0.0", 1);
  setenv("CARROT_GATEWAY_HTTP_LISTENER_PORT", "8081", 1);
  setenv("CARROT_GATEWAY_NODE_CONNECTIONS__0__ADDRESS", "10.0.0.1:9090", 1);
  setenv("CARROT_GATEWAY_NODE_CONNECTIONS__1__ADDRESS", "10.0.0.2:9090", 1);

  GatewayConfig config;
  ConfigLoadResult result = LoadConfig<GatewayConfig>("", {}, &config);
  EXPECT_TRUE(result.success) << result.error_message;
  ASSERT_GE(config.node_connections_size(), 2);
  EXPECT_EQ(config.node_connections(0).address(), "10.0.0.1:9090");
  EXPECT_EQ(config.node_connections(1).address(), "10.0.0.2:9090");

  unsetenv("CARROT_GATEWAY_HTTP_LISTENER_ADDRESS");
  unsetenv("CARROT_GATEWAY_HTTP_LISTENER_PORT");
  unsetenv("CARROT_GATEWAY_NODE_CONNECTIONS__0__ADDRESS");
  unsetenv("CARROT_GATEWAY_NODE_CONNECTIONS__1__ADDRESS");
}

TEST(ConfigLoaderTest, ValidateConfig) {
  GatewayConfig config;
  config.mutable_http_listener()->set_address("0.0.0.0");
  config.mutable_http_listener()->set_port(8081);
  config.mutable_logging()->set_level("info");

  ConfigLoadResult result = ValidateConfig(config);
  EXPECT_TRUE(result.success);
}

TEST(ConfigLoaderTest, ValidateConfigInvalidPort) {
  GatewayConfig config;
  config.mutable_http_listener()->set_address("0.0.0.0");
  config.mutable_http_listener()->set_port(99999);

  ConfigLoadResult result = ValidateConfig(config);
  EXPECT_FALSE(result.success);
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

  GatewayConfig config;
  ConfigLoadResult result = LoadConfig<GatewayConfig>(path, {}, &config);
  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_GE(result.warnings.size(), 1);
  bool found = false;
  for (const auto& w : result.warnings) {
    if (w.find("unknown_field") != std::string::npos) found = true;
  }
  EXPECT_TRUE(found);

  std::filesystem::remove(path);
}

TEST(ConfigLoaderTest, MissingYamlFile) {
  GatewayConfig config;
  ConfigLoadResult result = LoadConfig<GatewayConfig>("/nonexistent/config.yaml", {}, &config);
  EXPECT_FALSE(result.success);
}

TEST(ConfigLoaderTest, NodeAgentCliOverrides) {
  NodeAgentConfig config;
  ConfigLoadResult result = LoadConfig<NodeAgentConfig>(
      "", {"tlv_listener.port=7070", "tlv_listener.address=0.0.0.0"}, &config);
  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(config.tlv_listener().port(), 7070u);
  EXPECT_EQ(config.tlv_listener().address(), "0.0.0.0");
}

TEST(ConfigLoaderTest, GetDefaultConfig) {
  GatewayConfig config = GetDefaultConfig<GatewayConfig>();
  EXPECT_EQ(config.http_listener().port(), 0u);
}

} // namespace
} // namespace carrot::config
