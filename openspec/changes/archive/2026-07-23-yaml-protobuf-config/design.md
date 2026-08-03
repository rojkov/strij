# Design: YAML + Protobuf Config for Gateway & NodeAgent

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                        STRIJ CONFIG LAYER                          │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────────┐    ┌──────────────┐    ┌────────────────────┐   │
│  │   gateway    │    │  nodeagent   │    │  Shared Config Lib │   │
│  │  (exe)       │    │  (exe)       │    │  (core/config)     │   │
│  └──────┬───────┘    └──────┬───────┘    └─────────┬──────────┘   │
│         │                   │                      │              │
│         └───────────────────┼──────────────────────┘              │
│                             ▼                                     │
│              ┌────────────────────────────────┐                  │
│              │     ConfigLoader               │                  │
│              │  (loads YAML → validates →     │                  │
│              │   returns protobuf-generated   │                  │
│              │    C++ structs)                │                  │
│              └─────────────┬──────────────────┘                  │
│                            │                                     │
│         ┌──────────────────┼──────────────────┐                  │
│         ▼                  ▼                  ▼                  │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐         │
│  │  gateway.   │    │ nodeagent.  │    │  Defaults   │         │
│  │  proto      │    │  proto      │    │  (built-in) │         │
│  └─────────────┘    └─────────────┘    └─────────────┘         │
│         │                  │                  │                  │
│         └──────────────────┼──────────────────┘                  │
│                            ▼                                     │
│              ┌────────────────────────────────┐                  │
│              │  Merged Config (priority order)│                  │
│              │  1. Defaults (compile-time)    │                  │
│              │  2. YAML file                  │                  │
│              │  3. Env vars (STRIJ_*)        │                  │
│              │  4. CLI flags (--field=value)  │                  │
│              └────────────────────────────────┘                  │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

## Protobuf Schema Definitions

### gateway.proto

```protobuf
syntax = "proto3";

package strij.config;

import "google/protobuf/duration.proto";

message GatewayConfig {
  // HTTP listener configuration
  HttpListener http_listener = 1;

  // Node agent connections
  repeated NodeConnection node_connections = 2;

  // Logging configuration
  Logging logging = 3;

  // Timeouts (RESERVED - not used in v1, no TLS/timeout support in io layer yet)
  // google.protobuf.Duration connection_timeout = 4;
  // google.protobuf.Duration request_timeout = 5;

  // TLS (RESERVED - not implemented in v1)
  // TlsConfig tls = 6;
}

message HttpListener {
  string address = 1;      // default: "0.0.0.0"
  uint32 port = 2;         // default: 8081
  // RESERVED for future: uint32 max_connections = 3;
  // RESERVED for future: bool reuse_port = 4;
}

message NodeConnection {
  string address = 1;      // e.g., "127.0.0.1:9090"
  // RESERVED for future reconnect logic (NodeDirectory doesn't use these yet):
  // uint32 connect_timeout_ms = 2;
  // uint32 max_reconnect_attempts = 3;
  // uint32 reconnect_backoff_ms = 4;
}

message Logging {
  string level = 1;        // "trace" | "debug" | "info" | "warn" | "error" (default: "info")
  string format = 2;       // "text" | "json" (default: "text")
  string output = 3;       // "stdout" | "stderr" (default: "stdout")
  // RESERVED: string file_path = 4;  // if output=file (not implemented)
  bool include_source_location = 5; // default: false
}

// RESERVED - TLS not implemented in v1
// message TlsConfig {
//   bool enabled = 1;
//   string cert_file = 2;
//   string key_file = 3;
//   string ca_file = 4;
//   bool verify_peer = 5;
// }
```

### nodeagent.proto

```protobuf
syntax = "proto3";

package strij.config;

import "google/protobuf/duration.proto";

message NodeAgentConfig {
  TlvListener tlv_listener = 1;
  Logging logging = 2;
  // RESERVED - not used in v1:
  // google.protobuf.Duration connection_timeout = 3;
  // google.protobuf.Duration heartbeat_interval = 4;
  // RESERVED - TLS not implemented in v1:
  // TlsConfig tls = 5;
}

message TlvListener {
  string address = 1;      // default: "0.0.0.0"
  uint32 port = 2;         // default: 9090
  // RESERVED for future: uint32 max_connections = 3;
  // RESERVED for future: bool reuse_port = 4;
  // RESERVED for future: uint32 read_buffer_size = 5;
}

message Logging {
  string level = 1;        // "trace" | "debug" | "info" | "warn" | "error" (default: "info")
  string format = 2;       // "text" | "json" (default: "text")
  string output = 3;       // "stdout" | "stderr" (default: "stdout")
  // RESERVED: string file_path = 4;  // if output=file (not implemented)
  bool include_source_location = 5; // default: false
}

// RESERVED - TLS not implemented in v1
// message TlsConfig {
//   bool enabled = 1;
//   string cert_file = 2;
//   string key_file = 3;
//   string ca_file = 4;
//   bool verify_peer = 5;
// }
```

## YAML Configuration Examples

### gateway.yaml

```yaml
http_listener:
  address: "0.0.0.0"
  port: 8081

node_connections:
  - address: "127.0.0.1:9090"
  - address: "10.0.1.5:9090"

logging:
  level: "info"
  format: "json"
  output: "stdout"
  include_source_location: false

# RESERVED for future (not used in v1):
# connection_timeout: "30s"
# request_timeout: "60s"
# tls:
#   enabled: false
```

### nodeagent.yaml

```yaml
tlv_listener:
  address: "0.0.0.0"
  port: 9090

logging:
  level: "debug"
  format: "text"
  output: "stdout"

# RESERVED for future (not used in v1):
# connection_timeout: "30s"
# heartbeat_interval: "10s"
# tls:
#   enabled: false
```

## Config Loader Library Design

### `src/core/config/config_loader.hh`

```cpp
#pragma once

#include <string>
#include <optional>
#include <expected>
#include "gateway.pb.h"
#include "nodeagent.pb.h"

namespace strij::config {

struct LoadOptions {
  std::string config_file_path;
  bool allow_env_overrides = true;
  bool allow_cli_overrides = true;
};

class ConfigLoader {
public:
  // Load GatewayConfig from YAML file with overrides
  static auto LoadGatewayConfig(const LoadOptions& opts)
      -> std::expected<strij::config::GatewayConfig, std::string>;

  // Load NodeAgentConfig from YAML file with overrides
  static auto LoadNodeAgentConfig(const LoadOptions& opts)
      -> std::expected<strij::config::NodeAgentConfig, std::string>;

private:
  // Internal: merge YAML → protobuf with priority handling
  template <typename ProtoMsg>
  static auto LoadConfig(const LoadOptions& opts, ProtoMsg* msg)
      -> std::expected<void, std::string>;
};
```

### Priority Merge Logic

```cpp
// Pseudocode for priority merge
Config LoadConfig(opts):
  msg = DefaultConfig()                    // 1. Compile-time defaults
  if file_exists(opts.config_file_path):
    yaml = ParseYaml(opts.config_file_path)
    MergeYamlIntoProto(yaml, msg)          // 2. YAML file
  if opts.allow_env_overrides:
    MergeEnvVarsIntoProto(msg)             // 3. STRIJ_* env vars
  if opts.allow_cli_overrides:
    MergeCliFlagsIntoProto(msg)            // 4. Abseil flags
  Validate(msg)                            // Required fields, ranges
  return msg
```

## CLI Flag Integration (Abseil)

```cpp
// In gateway.cc / nodeagent.cc
ABSL_FLAG(std::string, config_file, "gateway.yaml", "Path to YAML config file");
ABSL_FLAG(bool, validate_only, false, "Validate config and exit");
ABSL_FLAG(std::string, log_level, "", "Override log level (trace|debug|info|warn|error)");
ABSL_FLAG(uint32_t, http_port, 0, "Override HTTP listener port (0 = use config)");
// ... etc for other common overrides
```

Env var mapping (auto-generated from flag names):
- `--config_file` → `STRIJ_CONFIG_FILE`
- `--http_port` → `STRIJ_HTTP_PORT`
- `--log_level` → `STRIJ_LOG_LEVEL`

## Bazel Build Integration

### MODULE.bazel additions

```python
bazel_dep(name = "protobuf", version = "33.4")
bazel_dep(name = "rules_proto", version = "7.1.0")
bazel_dep(name = "yaml_cpp", version = "0.8.0")
```

### BUILD.bazel for protobuf

```python
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_library")

proto_library(
    name = "gateway_config_proto",
    srcs = ["gateway.proto"],
    deps = ["@protobuf//:well_known_types"],
)

proto_library(
    name = "nodeagent_config_proto",
    srcs = ["nodeagent.proto"],
    deps = ["@protobuf//:well_known_types"],
)

# Generated C++ libraries
cc_library(
    name = "gateway_config_cc",
    srcs = [":gateway_config_proto"],
    deps = [
        "@protobuf//:protobuf",
        "@protobuf//:protobuf_lite",
    ],
)

cc_library(
    name = "nodeagent_config_cc",
    srcs = [":nodeagent_config_proto"],
    deps = [
        "@protobuf//:protobuf",
        "@protobuf//:protobuf_lite",
    ],
)
```

### Config library BUILD.bazel

```python
strij_cc_library(
    name = "config_loader_lib",
    srcs = ["config_loader.cc"],
    hdrs = ["config_loader.hh"],
    deps = [
        "//src/core/config/proto:gateway_config_cc",
        "//src/core/config/proto:nodeagent_config_cc",
        "@yaml_cpp//:yaml_cpp",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:usage",
        "@protobuf//:protobuf",
    ],
)
```

## Integration in gateway.cc / nodeagent.cc

### gateway.cc (new main)

```cpp
auto main(int argc, char** argv) -> int {
  absl::ParseCommandLine(argc, argv);
  
  auto config_result = config::ConfigLoader::LoadGatewayConfig({
    .config_file_path = absl::GetFlag(FLAGS_config_file),
  });
  
  if (!config_result) {
    LOG_ERROR() << "Config error: " << config_result.error();
    return 1;
  }
  
  const auto& config = config_result.value();
  
  if (absl::GetFlag(FLAGS_validate_only)) {
    LOG_INFO() << "Config valid";
    return 0;
  }
  
  // Use config values
  strij::io::TcpListener http_listener{
      dispatcher, config.http_listener().port(),
      /* handler factory */};
  
  strij::io::NodeDirectory node_directory{
      dispatcher, 
      ExtractAddresses(config.node_connections()),
      connection_factory};
  
  dispatcher->Run();
  return 0;
}
```

## Validation Rules (v1 Scope)

| Field | Validation |
|-------|------------|
| `port` | 1-65535 |
| `address` | Valid IP or hostname |
| `level` | One of: trace, debug, info, warn, error |
| `format` | One of: text, json |
| `output` | One of: stdout, stderr |
| `node_connections[N].address` | Required, host:port format |

### Reserved for Future (parsed but not validated for use)
- `connection_timeout`, `request_timeout`, `heartbeat_interval` — parsed as Duration, > 0
- `max_connections`, `reuse_port`, `read_buffer_size` — parsed, range validated
- `max_reconnect_attempts`, `reconnect_backoff_ms`, `connect_timeout_ms` — parsed, range validated
- `logging.output = "file"` + `file_path` — validated if present
- `tls.*` — all fields parsed, cross-validated (enabled → cert/key required)

## Error Messages

```
Error loading gateway.yaml: 
  [gateway.http_listener.port] value 99999 out of range (1-65535)
  [gateway.node_connections[1].address] invalid address format: "not-an-ip:port"
  [gateway.logging.level] unknown level "verbose", expected: trace|debug|info|warn|error
```

## Future Extensions

1. **Config hot-reload** - SIGHUP triggers reload, applies to logging level, timeouts (not listener ports)
2. **Config via TLV** - Gateway pushes config to nodeagents over existing TLV connection
3. **Config versioning** - Protobuf `package strij.config.v1`; add `v2` when breaking changes needed
4. **Secrets integration** - `value_from: "env:SECRET_KEY"` or `file:/run/secrets/tls.key` in YAML