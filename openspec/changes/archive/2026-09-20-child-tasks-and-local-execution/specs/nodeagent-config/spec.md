# nodeagent-config

## MODIFIED Requirements

### Requirement: NodeAgentConfig protobuf schema

The system SHALL define a `NodeAgentConfig` protobuf message in `api/nodeagent/config/nodeagent.proto` (package `strij.config`) with `TlvListener tlv_listener`, `Logging logging`, `repeated ExtensionConfig task_handlers`, `repeated NodeSchedulerConfig schedulers`, an optional `gateway_client` section declaring outbound gateway addresses, `repeated ResourcePool pools`, `repeated PoolReservation reservations`, an active `heartbeat_interval` field (state-snapshot cadence), and reserved-for-future fields `connection_timeout` and `TlsConfig tls`. Operator-declared handler capacity (concurrency limit only) SHALL be carried inside each task handler extension's `typed_config` as a shared `HandlerCapacity` message (defined in `node/capabilities.proto`), not as a separate top-level `handlers` section. `HandlerCapacity` SHALL NOT carry resource requirements: hardware requirements are resolved at the gateway and carried on the submitted `Task`. `schedulers` entries SHALL be a node-side `NodeSchedulerConfig { ExtensionConfig extension; string task_type; bool local_default; }`, distinct from the gateway `SchedulerConfig`: `task_type` is an optional local-authority declaration, `local_default` is an explicit fallback marker (at most one entry), and an empty `task_type` SHALL NOT select a default.

#### Scenario: Listener and logging sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with `tlv_listener`, `logging`, and `schedulers` sections
- **THEN** the resulting message SHALL contain the corresponding `TlvListener`, `Logging`, and `NodeSchedulerConfig` values

#### Scenario: Scheduler sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with a `schedulers` list containing entries with `extension.name`, `extension.typed_config`, `task_type`, and `local_default`
- **THEN** the resulting message SHALL contain each `NodeSchedulerConfig` entry with its nested `ExtensionConfig`, `task_type`, and `local_default`

#### Scenario: Task handler sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with a `task_handlers` list containing entries with `name` and `typed_config`
- **THEN** the resulting message SHALL contain each `ExtensionConfig` entry with its `name` and packed `typed_config`

#### Scenario: Gateway client section loads into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with a `gateway_client` section containing `addresses`
- **THEN** the resulting message SHALL contain the configured addresses

#### Scenario: Capability sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with `pools` and `reservations` sections, and a `task_handlers` entry whose `typed_config` carries `capacity`
- **THEN** the resulting message SHALL contain the corresponding `ResourcePool` and `PoolReservation` values, and the unpacked handler config SHALL expose the declared `HandlerCapacity`

#### Scenario: TLS fields are reserved for future use

- **WHEN** a future release enables TLS on the nodeagent
- **THEN** the reserved `TlsConfig tls` field SHALL carry cert/key/ca/verify-peer settings without a breaking schema change

### Requirement: Local scheduler configuration

The nodeagent SHALL load one local scheduler instance per `NodeAgentConfig.schedulers` entry. The `schedulers` list SHALL be required and non-empty: when unset or empty, the nodeagent SHALL fail to start with an error indicating a scheduler must be configured. The nodeagent SHALL fail to start when any configured scheduler name is not registered in `Registry<NodeSchedulerFactory>`. Local scheduling authority SHALL be declared explicitly: a non-empty `task_type` makes the entry authoritative over locally-originated tasks of that type; `local_default = true` makes the entry the node's fallback authority for locally-originated tasks no other entry claims; entries with neither SHALL not schedule locally-originated tasks (pure wire-protocol counterparts). An empty `task_type` SHALL NOT act as a default. The nodeagent SHALL fail to start on a duplicate non-empty `task_type`, on more than one `local_default`, or when child submission would route to an unmatched type with no local default declared.

#### Scenario: Missing schedulers fails startup

- **WHEN** a `NodeAgentConfig` has no `schedulers` section (or an empty list)
- **THEN** the nodeagent SHALL log an error and exit with status 1

#### Scenario: Multiple schedulers construct one instance each

- **WHEN** a `NodeAgentConfig` sets `schedulers` with two entries, one `"push"` and one `"probe"`
- **THEN** the nodeagent SHALL construct two local scheduler instances, one per entry
- **AND** each instance SHALL implement the protocol of its entry's factory

#### Scenario: Unknown local scheduler name fails startup

- **WHEN** a `NodeAgentConfig` sets `schedulers[0].extension.name = "nonexistent"` and no such factory is registered in `Registry<NodeSchedulerFactory>`
- **THEN** the nodeagent SHALL log an error and exit with status 1

#### Scenario: Type-claimed child scheduling routes by task type

- **WHEN** a `NodeSchedulerConfig` entry declares `task_type = "workflow"` and another entry declares `local_default = true`
- **THEN** child submissions of type `"workflow"` SHALL route to the claiming entry
- **AND** child submissions of other types SHALL route to the `local_default` entry

#### Scenario: Entry without a local role schedules nothing locally

- **WHEN** a `NodeSchedulerConfig` entry declares neither `task_type` nor `local_default`
- **THEN** it SHALL be constructed as a wire-protocol counterpart
- **AND** it SHALL NOT receive any locally-originated task submission

#### Scenario: Bundled default scheduler is the local authority

- **WHEN** a `schedulers` entry declares extension name `"default"` with `local_default = true` and no `typed_config` (its `DefaultSchedulerConfig` is empty)
- **THEN** the nodeagent SHALL construct the bundled `default` local scheduler
- **AND** its `Schedule` SHALL implement the child-task policy (run locally, else forward)
- **AND** its `RequiredProtocol()` SHALL be empty (it is declared by the node's wire-protocol entries)

#### Scenario: Ambiguous local-authority declarations fail startup

- **WHEN** two `schedulers` entries declare the same non-empty `task_type` or more than one entry declares `local_default = true`
- **THEN** the nodeagent SHALL fail to start with an error describing the ambiguity

## ADDED Requirements

### Requirement: Gateway client configuration section

`NodeAgentConfig` SHALL define a `gateway_client` section with a `repeated string addresses` field naming outbound gateway endpoints for the child-forward path. The section SHALL be optional and additive; its absence SHALL NOT change validation of existing configurations. The addresses are reserved for a future outbound dial fallback (`gateway-client`); they SHALL NOT be required for the forward path, which operates over live registered connections.

#### Scenario: Gateway client is optional

- **WHEN** a `NodeAgentConfig` omits the `gateway_client` section
- **THEN** the nodeagent SHALL start normally with an empty address list
- **AND** forwarding SHALL operate over live connections only

#### Scenario: Gateway client addresses load and validate

- **WHEN** a `NodeAgentConfig` declares `gateway_client.addresses`
- **THEN** the nodeagent SHALL parse the list and validate that each entry is a non-empty string
- **AND** an empty address SHALL fail validation
