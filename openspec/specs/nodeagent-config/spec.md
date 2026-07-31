# NodeAgent Config Capability

## Purpose

Protobuf schema and default configuration for the Carrot NodeAgent service: defines all nodeagent-configurable parameters, their validation constraints and default values, plus cross-field rules such as heartbeat interval validation against the connection timeout.

## Requirements

### Requirement: NodeAgentConfig protobuf schema

The system SHALL define a `NodeAgentConfig` protobuf message in `src/core/config/proto/nodeagent.proto` (package `carrot.config`) with `TlvListener tlv_listener`, `Logging logging`, and reserved-for-future fields `connection_timeout`, `heartbeat_interval`, and `TlsConfig tls`.

#### Scenario: Listener and logging sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with `tlv_listener` and `logging` sections
- **THEN** the resulting message SHALL contain the corresponding `TlvListener` and `Logging` values

#### Scenario: TLS fields are reserved for future use

- **WHEN** a future release enables TLS on the nodeagent
- **THEN** the reserved `TlsConfig tls` field SHALL carry cert/key/ca/verify-peer settings without a breaking schema change

### Requirement: Field validation constraints

All nodeagent config fields SHALL carry appropriate validation constraints via the custom options `(carrot.config.required)`, `(carrot.config.range_min)`, `(carrot.config.range_max)`, `(carrot.config.enum_values)`, and `(carrot.config.pattern)`.

#### Scenario: Invalid port rejected

- **WHEN** a YAML config sets `tlv_listener.port` to 70000
- **THEN** loading SHALL fail with a validation error indicating the value exceeds the maximum 65535

#### Scenario: TLS enabled without certificates

- **WHEN** `tls.enabled` is true and `cert_file`/`key_file` are empty
- **THEN** loading SHALL fail with a validation error

### Requirement: Default values allow startup without a config file

Default values SHALL allow the nodeagent to run without a config file (with warnings): `tlv_listener.address` defaults to `"0.0.0.0"`, `tlv_listener.port` to `9090`, `logging.level` to `"info"`, and `logging.format` to `"text"`.

#### Scenario: Default port applied

- **WHEN** a `NodeAgentConfig` is created with no explicit `tlv_listener.port`
- **THEN** `tlv_listener.port` SHALL be `9090`

#### Scenario: Default address applied

- **WHEN** a `NodeAgentConfig` is created with no explicit `tlv_listener.address`
- **THEN** `tlv_listener.address` SHALL be `"0.0.0.0"`

### Requirement: Heartbeat interval validated against connection timeout

The system SHALL validate that `heartbeat_interval <= connection_timeout` (a heartbeat must fire before the timeout). The system SHALL also warn if `read_buffer_size` is smaller than a typical TLV frame (below 16KB).

#### Scenario: Heartbeat interval exceeds connection timeout

- **WHEN** `heartbeat_interval` is greater than `connection_timeout`
- **THEN** loading SHALL fail with a validation error

#### Scenario: Small read buffer warns

- **WHEN** `read_buffer_size` is set below 16KB
- **THEN** loading SHALL succeed with a warning

### Requirement: NodeAgentConfig is extensible

The schema SHALL support future TLS, metrics, and other extensions through reserved fields, without redefining existing field numbers.

#### Scenario: Future extensions use reserved fields

- **WHEN** a new extension category is introduced
- **THEN** it SHALL use a new reserved field number or the `ExtensionConfig` mechanism
- **AND** existing fields SHALL keep their numbers and semantics

## Protobuf Schema

File: `src/core/config/proto/nodeagent.proto`

```protobuf
syntax = "proto3";

package carrot.config;

import "google/protobuf/duration.proto";
import "carrot/config/options.proto";

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
    (carrot.config.required) = true,
    (carrot.config.range_min) = "1",
    (carrot.config.range_max) = "65535"
  ];
  // RESERVED for future (v2+)
  uint32 max_connections = 3 [
    default = 10000,
    (carrot.config.range_min) = "1",
    (carrot.config.range_max) = "1000000"
  ];
  bool reuse_port = 4 [default = true];
  uint32 read_buffer_size = 5 [
    default = 65536,
    (carrot.config.range_min) = "4096",
    (carrot.config.range_max) = "1048576"
  ];
}

message Logging {
  string level = 1 [
    default = "info",
    (carrot.config.enum_values) = "trace",
    (carrot.config.enum_values) = "debug",
    (carrot.config.enum_values) = "info",
    (carrot.config.enum_values) = "warn",
    (carrot.config.enum_values) = "error"
  ];
  string format = 2 [
    default = "text",
    (carrot.config.enum_values) = "text",
    (carrot.config.enum_values) = "json"
  ];
  string output = 3 [
    default = "stdout",
    (carrot.config.enum_values) = "stdout",
    (carrot.config.enum_values) = "stderr"
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
4. `read_buffer_size` should be >= typical TLV frame size (warn if < 16KB)
