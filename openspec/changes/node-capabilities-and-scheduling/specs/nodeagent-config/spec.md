# NodeAgent Config Capability

## Purpose

Protobuf schema and default configuration for the Strij NodeAgent service: defines all nodeagent-configurable parameters, their validation constraints and default values, plus cross-field rules such as heartbeat interval validation against the connection timeout.

## MODIFIED Requirements

### Requirement: NodeAgentConfig protobuf schema

The system SHALL define a `NodeAgentConfig` protobuf message in `api/core/config/nodeagent.proto` (package `strij.config`) with `TlvListener tlv_listener`, `Logging logging`, `repeated ExtensionConfig task_handlers`, `repeated ResourcePool pools`, `repeated PoolReservation reservations`, `repeated HandlerCapability handlers`, an active `heartbeat_interval` field (state-snapshot cadence), and reserved-for-future fields `connection_timeout` and `TlsConfig tls`.

#### Scenario: Listener and logging sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with `tlv_listener` and `logging` sections
- **THEN** the resulting message SHALL contain the corresponding `TlvListener` and `Logging` values

#### Scenario: Task handler sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with a `task_handlers` list containing entries with `name` and `typed_config`
- **THEN** the resulting message SHALL contain each `ExtensionConfig` entry with its `name` and packed `typed_config`

#### Scenario: Capability sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with `pools`, `reservations`, and `handlers` sections
- **THEN** the resulting message SHALL contain the corresponding `ResourcePool`, `PoolReservation`, and `HandlerCapability` values

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

The nodeagent SHALL derive its `NodeCapabilities` advertisement from `NodeAgentConfig.pools`, `.reservations`, and `.handlers`, plus the handlers registered in `TaskHandlerManager`. The nodeagent SHALL fail to start if a `PoolReservation` references a pool not declared in `pools`, or if a `HandlerCapability` names a task type with no registered handler.

#### Scenario: Advertisement reflects configured pools and handlers

- **WHEN** a nodeagent is configured with pool `cpu` total 16 and a handler capability for type `"echo"`
- **THEN** its `NodeCapabilities` SHALL contain pool `cpu` with total 16 and a handler capability for type `"echo"`

#### Scenario: Reservation referencing an undeclared pool fails startup

- **WHEN** a `PoolReservation` references a pool that is not in `pools`
- **THEN** startup SHALL fail with a validation error

#### Scenario: Handler capability without a registered handler fails startup

- **WHEN** a `HandlerCapability` names a task type with no handler in `TaskHandlerManager`
- **THEN** startup SHALL fail with a validation error
