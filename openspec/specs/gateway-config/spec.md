# Gateway Config Capability

## Purpose

Protobuf schema and default configuration for the Carrot Gateway service: defines all gateway-configurable parameters, their validation constraints and default values, plus cross-field rules such as requiring the `node_discovery` extension.

## Requirements

### Requirement: GatewayConfig protobuf schema

The system SHALL define a `GatewayConfig` protobuf message in `src/core/config/proto/gateway.proto` (package `carrot.config`) with `HttpListener http_listener`, `repeated NodeConnection node_connections`, `Logging logging`, and reserved-for-future fields `connection_timeout`, `request_timeout`, and `TlsConfig tls`.

#### Scenario: Listener and logging sections load into the message

- **WHEN** a `GatewayConfig` is loaded from YAML with `http_listener` and `logging` sections
- **THEN** the resulting message SHALL contain the corresponding `HttpListener` and `Logging` values

#### Scenario: TLS fields are reserved for future use

- **WHEN** a future release enables TLS on the gateway
- **THEN** the reserved `TlsConfig tls` field SHALL carry cert/key/ca/verify-peer settings without a breaking schema change

### Requirement: Field validation constraints

All gateway config fields SHALL carry appropriate validation constraints via the custom options `(carrot.config.required)`, `(carrot.config.range_min)`, `(carrot.config.range_max)`, `(carrot.config.enum_values)`, and `(carrot.config.pattern)`.

#### Scenario: Invalid port rejected

- **WHEN** a YAML config sets `http_listener.port` to 70000
- **THEN** loading SHALL fail with a validation error indicating the value exceeds the maximum 65535

#### Scenario: Node address pattern enforced

- **WHEN** a YAML config sets a `node_connection.address` that does not match `^[^:]+:\d+$`
- **THEN** loading SHALL fail with a validation error

### Requirement: Default values allow startup without a config file

Default values SHALL allow the gateway to run without a config file (with warnings): `http_listener.address` defaults to `"0.0.0.0"`, `http_listener.port` to `8081`, `logging.level` to `"info"`, and `logging.format` to `"text"`.

#### Scenario: Default port applied

- **WHEN** a `GatewayConfig` is created with no explicit `http_listener.port`
- **THEN** `http_listener.port` SHALL be `8081`

#### Scenario: Default address applied

- **WHEN** a `GatewayConfig` is created with no explicit `http_listener.address`
- **THEN** `http_listener.address` SHALL be `"0.0.0.0"`

### Requirement: Cross-field validation catches misconfiguration

The system SHALL validate cross-field constraints: if `tls.enabled` is true then `cert_file` and `key_file` must be non-empty; if `logging.output == "file"` then `logging.file_path` must be non-empty; and `request_timeout` must be `>= connection_timeout` (or both use defaults).

#### Scenario: TLS enabled without certificates

- **WHEN** `tls.enabled` is true and `cert_file`/`key_file` are empty
- **THEN** loading SHALL fail with a validation error

### Requirement: node_discovery required

The system SHALL require the `node_discovery` extension in `GatewayConfig`; the gateway SHALL exit with an error if it is absent. The `node_connections` field SHALL be retained for wire compatibility only and ignored at runtime.

#### Scenario: Missing node_discovery fails validation

- **WHEN** a `GatewayConfig` is loaded without a `node_discovery` section
- **THEN** validation SHALL fail with an error indicating the extension is required

### Requirement: GatewayConfig is extensible

The schema SHALL support future TLS, metrics, and rate-limiting extensions through reserved fields and the extension mechanism, without redefining existing field numbers.

#### Scenario: Future extensions use reserved fields

- **WHEN** a new extension category is introduced
- **THEN** it SHALL use a new reserved field number or the `ExtensionConfig` mechanism
- **AND** existing fields SHALL keep their numbers and semantics

## Protobuf Schema

File: `src/core/config/proto/gateway.proto`

```protobuf
syntax = "proto3";

package carrot.config;

import "google/protobuf/duration.proto";
import "carrot/config/options.proto";

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
}

message NodeConnection {
  string address = 1 [
    (carrot.config.required) = true,
    (carrot.config.pattern) = "^[^:]+:\\d+$"
  ];
  // RESERVED for future (v2+): NodeDirectory doesn't use these yet
  uint32 connect_timeout_ms = 2 [
    default = 5000,
    (carrot.config.range_min) = "100",
    (carrot.config.range_max) = "300000"
  ];
  uint32 max_reconnect_attempts = 3 [
    default = 3,
    (carrot.config.range_min) = "0",
    (carrot.config.range_max) = "100"
  ];
  uint32 reconnect_backoff_ms = 4 [
    default = 1000,
    (carrot.config.range_min) = "100",
    (carrot.config.range_max) = "60000"
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
4. `node_discovery` extension MUST be configured (error if missing)
5. `node_connections` field is ignored — retained for wire compatibility only
