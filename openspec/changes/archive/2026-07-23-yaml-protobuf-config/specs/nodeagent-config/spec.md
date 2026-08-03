# NodeAgent Config Capability

## Overview
Protobuf schema and default configuration for the Strij NodeAgent service.

## Protobuf Schema
File: `src/core/config/proto/nodeagent.proto`

```protobuf
syntax = "proto3";

package strij.config;

import "google/protobuf/duration.proto";
import "strij/config/options.proto";

message NodeAgentConfig {
  TlvListener tlv_listener = 1;
  Logging logging = 2;
  // RESERVED for future (v2+): not used by nodeagent in v1
  google.protobuf.Duration connection_timeout = 3;
  google.protobuf.Duration heartbeat_interval = 4;
  TlsConfig tls = 5;
}

message TlvListener {
  string address = 1 [default = "0.0.0.0"];
  uint32 port = 2 [
    default = 9090,
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
  uint32 read_buffer_size = 5 [
    default = 65536,
    (strij.config.range_min) = "4096",
    (strij.config.range_max) = "1048576"
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
- `tlv_listener.address`: "0.0.0.0"
- `tlv_listener.port`: 9090
- `tlv_listener.max_connections`: 10000
- `tlv_listener.reuse_port`: true
- `tlv_listener.read_buffer_size`: 65536
- `logging.level`: "info"
- `logging.format`: "text"
- `logging.output`: "stdout"
- `logging.include_source_location`: false
- `connection_timeout`: 30s (Duration default)
- `heartbeat_interval`: 10s (Duration default)
- `tls.enabled`: false
- `tls.verify_peer`: true

## Cross-Field Validation Rules
1. If `tls.enabled == true`: `cert_file` and `key_file` must be non-empty
2. If `logging.output == "file"`: `logging.file_path` must be non-empty
3. `heartbeat_interval <= connection_timeout` (heartbeat must fire before timeout)
3. `read_buffer_size` should be >= typical TLV frame size (warn if < 16KB)

## Requirements
- **REQ-NA-001**: Protobuf schema defines all nodeagent-configurable parameters
- **REQ-NA-002**: All fields have appropriate validation constraints
- **REQ-NA-003**: Default values allow nodeagent to run without config file (with warnings)
- **REQ-NA-004**: Schema supports future TLS, metrics extensions
- **REQ-NA-005**: Heartbeat interval validated against connection timeout

## Dependencies
- `yaml-config-loader` capability (for loading)
- `protobuf` with `google/protobuf/duration.proto`
- Custom options: `strij/config/options.proto`

## Testing
- Unit test: Load default config (no YAML) → valid with warnings
- Unit test: Valid YAML with all fields → loads correctly
- Unit test: Invalid port → validation error
- Unit test: TLS enabled without certs → validation error
- Unit test: heartbeat_interval > connection_timeout → validation error
- Unit test: read_buffer_size too small → warning
- Integration: Full load with env/CLI overrides