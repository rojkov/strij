# NodeAgent Config Capability

## Purpose

Protobuf schema and default configuration for the Strij NodeAgent service: defines all nodeagent-configurable parameters, their validation constraints and default values, plus cross-field rules such as heartbeat interval validation against the connection timeout.

## Requirements

### Requirement: NodeAgentConfig protobuf schema

The system SHALL define a `NodeAgentConfig` protobuf message in `api/core/config/nodeagent.proto` (package `strij.config`) with `TlvListener tlv_listener`, `Logging logging`, `repeated ExtensionConfig task_handlers`, `repeated ResourcePool pools`, `repeated PoolReservation reservations`, an active `heartbeat_interval` field (state-snapshot cadence), and reserved-for-future fields `connection_timeout` and `TlsConfig tls`. Operator-declared handler capacity (concurrency limit and default resources) SHALL be carried inside each task handler extension's `typed_config` as a shared `HandlerCapacity` message (defined in `core/node/capabilities.proto`), not as a separate top-level `handlers` section.

#### Scenario: Listener and logging sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with `tlv_listener` and `logging` sections
- **THEN** the resulting message SHALL contain the corresponding `TlvListener` and `Logging` values

#### Scenario: Task handler sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with a `task_handlers` list containing entries with `name` and `typed_config`
- **THEN** the resulting message SHALL contain each `ExtensionConfig` entry with its `name` and packed `typed_config`

#### Scenario: Capability sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with `pools` and `reservations` sections, and a `task_handlers` entry whose `typed_config` carries `capacity`
- **THEN** the resulting message SHALL contain the corresponding `ResourcePool` and `PoolReservation` values, and the unpacked handler config SHALL expose the declared `HandlerCapacity`

#### Scenario: TLS fields are reserved for future use

- **WHEN** a future release enables TLS on the nodeagent
- **THEN** the reserved `TlsConfig tls` field SHALL carry cert/key/ca/verify-peer settings without a breaking schema change

### Requirement: Field validation constraints

All nodeagent config fields SHALL carry appropriate validation constraints via the custom options `(strij.config.required)`, `(strij.config.range_min)`, `(strij.config.range_max)`, `(strij.config.enum_values)`, and `(strij.config.pattern)`.

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

### Requirement: Heartbeat interval is the active state cadence

The reserved `heartbeat_interval` Duration field SHALL be active in v2 and SHALL control how often the nodeagent sends `kNodeState` snapshots on established connections (default 10s). The existing cross-field validation (`heartbeat_interval <= connection_timeout`) SHALL continue to apply.

#### Scenario: Default heartbeat interval applied

- **WHEN** a `NodeAgentConfig` is created with no explicit `heartbeat_interval`
- **THEN** `heartbeat_interval` SHALL be 10s

### Requirement: Pools are required in v1

In v1 the nodeagent SHALL require at least one `ResourcePool` to be declared in `NodeAgentConfig.pools`; the nodeagent SHALL fail to start if the list is empty. The pool source is a future extension point (e.g. `static_config`, auto-probing implementations); v1 supports config-declared pools only.

#### Scenario: Empty pools fail startup

- **WHEN** a `NodeAgentConfig` has no `pools` entries
- **THEN** the nodeagent SHALL fail to start with an error indicating pools must be configured

### Requirement: Nodeagent derives capabilities from config

The nodeagent SHALL derive its `NodeCapabilities` advertisement from `NodeAgentConfig.pools`, `.reservations`, and `.task_handlers`. Each task handler extension entry SHALL resolve to a registered `TaskHandlerFactory`; the nodeagent SHALL read the operator-declared capacity via the factory's `ParseConfig` method and advertise a `HandlerCapability` whose `task_type` is the factory name. The nodeagent SHALL fail to start if a `PoolReservation` references a pool not declared in `pools`, or if a task handler extension name does not resolve to a registered factory.

#### Scenario: Advertisement reflects configured pools and handlers

- **WHEN** a nodeagent is configured with pool `cpu` total 16 and a task handler extension for `"echo"` whose config declares capacity
- **THEN** its `NodeCapabilities` SHALL contain pool `cpu` with total 16 and a handler capability for type `"echo"` carrying the declared capacity

#### Scenario: Reservation referencing an undeclared pool fails startup

- **WHEN** a `PoolReservation` references a pool that is not in `pools`
- **THEN** startup SHALL fail with a validation error

#### Scenario: Handler capability without a registered handler fails startup

- **WHEN** a `task_handlers` entry names a task handler with no registered factory
- **THEN** startup SHALL fail with a validation error

## Protobuf Schema

File: `api/core/config/nodeagent.proto`

```protobuf
syntax = "proto3";

package strij.config;

import "google/protobuf/duration.proto";
import "strij/config/extensions.proto";
import "strij/config/options.proto";

message NodeAgentConfig {
  TlvListener tlv_listener = 1;
  Logging logging = 2;
  // RESERVED for future (v2+): not used by nodeagent in v1
  google.protobuf.Duration connection_timeout = 3;
  google.protobuf.Duration heartbeat_interval = 4;
  TlsConfig tls = 5;
  repeated ExtensionConfig task_handlers = 6;
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
4. `read_buffer_size` should be >= typical TLV frame size (warn if < 16KB)
