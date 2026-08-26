## MODIFIED Requirements

### Requirement: NodeAgentConfig protobuf schema

The system SHALL define a `NodeAgentConfig` protobuf message in `api/core/config/nodeagent.proto` (package `strij.config`) with `TlvListener tlv_listener`, `Logging logging`, `repeated ExtensionConfig task_handlers`, `repeated ExtensionConfig schedulers`, `repeated ResourcePool pools`, `repeated PoolReservation reservations`, an active `heartbeat_interval` field (state-snapshot cadence), and reserved-for-future fields `connection_timeout` and `TlsConfig tls`. Operator-declared handler capacity (concurrency limit only) SHALL be carried inside each task handler extension's `typed_config` as a shared `HandlerCapacity` message (defined in `core/node/capabilities.proto`), not as a separate top-level `handlers` section. `HandlerCapacity` SHALL NOT carry resource requirements: hardware requirements are resolved at the gateway and carried on the submitted `Task`.

#### Scenario: Listener and logging sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with `tlv_listener`, `logging`, and `schedulers` sections
- **THEN** the resulting message SHALL contain the corresponding `TlvListener`, `Logging`, and `schedulers` values

#### Scenario: Scheduler sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with a `schedulers` list containing entries with `name` and `typed_config`
- **THEN** the resulting message SHALL contain each `ExtensionConfig` entry with its `name` and packed `typed_config`

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

The nodeagent SHALL load one local scheduler instance per `NodeAgentConfig.schedulers` entry. The `schedulers` list SHALL be required and non-empty: when unset or empty, the nodeagent SHALL fail to start with an error indicating a scheduler must be configured. The nodeagent SHALL fail to start when any configured scheduler name is not registered in `Registry<NodeSchedulerFactory>`.

#### Scenario: Missing schedulers fails startup

- **WHEN** a `NodeAgentConfig` has no `schedulers` section (or an empty list)
- **THEN** the nodeagent SHALL log an error and exit with status 1

#### Scenario: Multiple schedulers construct one instance each

- **WHEN** a `NodeAgentConfig` sets `schedulers` with two entries, one `"push"` and one `"probe"`
- **THEN** the nodeagent SHALL construct two local scheduler instances, one per entry
- **AND** each instance SHALL implement the protocol of its entry's factory

#### Scenario: Unknown local scheduler name fails startup

- **WHEN** a `NodeAgentConfig` sets `schedulers[0].name = "nonexistent"` and no such factory is registered in `Registry<NodeSchedulerFactory>`
- **THEN** the nodeagent SHALL log an error and exit with status 1
