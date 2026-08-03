# Gateway Config Capability

## Overview
Protobuf schema and default configuration for the Strij Gateway service.

## Protobuf Schema
File: `src/core/config/proto/gateway.proto`

```protobuf
syntax = "proto3";

package strij.config;

import "google/protobuf/duration.proto";
import "strij/config/options.proto";

message GatewayConfig {
  HttpListener http_listener = 1;
  repeated NodeConnection node_connections = 2;
  Logging logging = 3;
  // RESERVED for future (v2+): not used by gateway in v1
  google.protobuf.Duration connection_timeout = 4;
  google.protobuf.Duration request_timeout = 5;
  TlsConfig tls = 6;
}

message HttpListener {
  string address = 1 [default = "0.0.0.0"];
  uint32 port = 2 [
    default = 8081,
    (strij.config.required) = true,
    (strij.config.range_min) = "1",
    (strij.config.range_max) = "65535"
  ];
  // RESERVED for future (v2+)
  uint32 max_connections = 3 [
    default = 10000,
    (strij.config.range_min) = "1",
    (strij.config.range_max) = "1000000"
  ];
  bool reuse_port = 4 [default = true];
}

message NodeConnection {
  string address = 1 [
    (strij.config.required) = true,
    (strij.config.pattern) = "^[^:]+:\\d+$"
  ];
  // RESERVED for future (v2+): NodeDirectory doesn't use these yet
  uint32 connect_timeout_ms = 2 [
    default = 5000,
    (strij.config.range_min) = "100",
    (strij.config.range_max) = "300000"
  ];
  uint32 max_reconnect_attempts = 3 [
    default = 3,
    (strij.config.range_min) = "0",
    (strij.config.range_max) = "100"
  ];
  uint32 reconnect_backoff_ms = 4 [
    default = 1000,
    (strij.config.range_min) = "100",
    (strij.config.range_max) = "60000"
  ];
}

message Logging {
  string level = 1 [
    default = "info",
    (strij.config.enum_values) = "trace",
    (strij.config.enum_values) = "debug",
    (strij.config.enum_values) = "info",
    (strij.config.enum_values) = "warn",
    (strij.config.enum_values) = "error"
  ];
  string format = 2 [
    default = "text",
    (strij.config.enum_values) = "text",
    (strij.config.enum_values) = "json"
  ];
  string output = 3 [
    default = "stdout",
    (strij.config.enum_values) = "stdout",
    (strij.config.enum_values) = "stderr"
    // "file" reserved for future
  ];
  string file_path = 4;  // Required if output="file" (future)
  bool include_source_location = 5 [default = false];
}

message TlsConfig {
  // RESERVED for future (v2+): no TLS implementation yet
  bool enabled = 1 [default = false];
  string cert_file = 2;
  string key_file = 3;
  string ca_file = 4;
  bool verify_peer = 5 [default = true];
}
```

## Default Values (Protobuf Defaults)
- `http_listener.address`: "0.0.0.0"
- `http_listener.port`: 8081
- `http_listener.max_connections`: 10000
- `http_listener.reuse_port`: true
- `node_connection.connect_timeout_ms`: 5000
- `node_connection.max_reconnect_attempts`: 3
- `node_connection.reconnect_backoff_ms`: 1000
- `logging.level`: "info"
- `logging.format`: "text"
- `logging.output`: "stdout"
- `logging.include_source_location`: false
- `connection_timeout`: 30s (Duration default)
- `request_timeout`: 60s (Duration default)
- `tls.enabled`: false
- `tls.verify_peer`: true

## Cross-Field Validation Rules
1. If `tls.enabled == true`: `cert_file` and `key_file` must be non-empty
2. If `logging.output == "file"`: `logging.file_path` must be non-empty
3. `request_timeout >= connection_timeout` (or both use defaults)
4. At least one `node_connection` must be configured (warn if empty)

## Requirements
- **REQ-GW-001**: Protobuf schema defines all gateway-configurable parameters
- **REQ-GW-002**: All fields have appropriate validation constraints
- **REQ-GW-003**: Default values allow gateway to run without config file (with warnings)
- **REQ-GW-004**: Schema supports future TLS, metrics, rate limiting extensions
- **REQ-GW-005**: Cross-field validation catches common misconfigurations

## Dependencies
- `yaml-config-loader` capability (for loading)
- `protobuf` with `google/protobuf/duration.proto`
- Custom options: `strij/config/options.proto`

## Testing
- Unit test: Load default config (no YAML) → valid with warnings
- Unit test: Valid YAML with all fields → loads correctly
- Unit test: Invalid port → validation error
- Unit test: TLS enabled without certs → validation error
- Unit test: Empty node_connections → warning
- Integration: Full load with env/CLI overrides