## MODIFIED Requirements

### Requirement: NodeAgentConfig protobuf schema

The system SHALL define a `NodeAgentConfig` protobuf message in `api/core/config/nodeagent.proto` (package `strij.config`) with `TlvListener tlv_listener`, `Logging logging`, `repeated ExtensionConfig task_handlers`, `ExtensionConfig scheduler`, `repeated ResourcePool pools`, `repeated PoolReservation reservations`, an active `heartbeat_interval` field (state-snapshot cadence), and reserved-for-future fields `connection_timeout` and `TlsConfig tls`. Operator-declared handler capacity (concurrency limit only) SHALL be carried inside each task handler extension's `typed_config` as a shared `HandlerCapacity` message (defined in `core/node/capabilities.proto`), not as a separate top-level `handlers` section. `HandlerCapacity` SHALL NOT carry resource requirements: hardware requirements are resolved at the gateway and carried on the submitted `Task`.

#### Scenario: Listener and logging sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with `tlv_listener`, `logging`, and `scheduler` sections
- **THEN** the resulting message SHALL contain the corresponding `TlvListener`, `Logging`, and `scheduler` values

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

### Requirement: Local scheduler configuration

The nodeagent SHALL load its single local scheduler from `NodeAgentConfig.scheduler`. When `scheduler` is unset, the nodeagent SHALL default to the built-in `"push"` local scheduler with an empty config, preserving pre-change behavior for existing configurations. The nodeagent SHALL fail to start when the configured scheduler name is not registered in `Registry<NodeSchedulerFactory>`.

#### Scenario: Unset scheduler defaults to push

- **WHEN** a `NodeAgentConfig` has no `scheduler` section
- **THEN** the nodeagent SHALL construct the `"push"` local scheduler
- **AND** SHALL advertise `"push"` in its `scheduling_protocols`

#### Scenario: Unknown local scheduler name fails startup

- **WHEN** a `NodeAgentConfig` sets `scheduler.name = "nonexistent"` and no such factory is registered in `Registry<NodeSchedulerFactory>`
- **THEN** the nodeagent SHALL log an error and exit with status 1
