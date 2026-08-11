# NodeAgent Config Capability

## Purpose

Protobuf schema and default configuration for the Strij NodeAgent service: defines all nodeagent-configurable parameters, their validation constraints and default values, plus cross-field rules such as heartbeat interval validation against the connection timeout.

## MODIFIED Requirements

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

## ADDED Requirements

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
