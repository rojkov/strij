## MODIFIED Requirements

### Requirement: GatewayConfig protobuf schema

The system SHALL define a `GatewayConfig` protobuf message in `api/core/config/gateway.proto` (package `strij.config`) with `HttpListener http_listener`, `repeated NodeConnection node_connections`, `Logging logging`, `ExtensionConfig node_discovery`, `repeated SchedulerConfig schedulers`, and reserved-for-future fields `connection_timeout`, `request_timeout`, and `TlsConfig tls`. The system SHALL define a `SchedulerConfig` message with `ExtensionConfig extension` (the `name` and packed `typed_config` of the scheduler) and an optional `string task_type`; a `SchedulerConfig` whose `task_type` is empty SHALL act as the default scheduler for task types matched by no other entry. At least one `SchedulerConfig` entry SHALL be required.

#### Scenario: Listener, logging, and scheduler sections load into the message

- **WHEN** a `GatewayConfig` is loaded from YAML with `http_listener`, `logging`, `node_discovery`, and `schedulers` sections
- **THEN** the resulting message SHALL contain the corresponding `HttpListener`, `Logging`, `node_discovery`, and `schedulers` values

#### Scenario: Scheduler loads as an ExtensionConfig with a task type

- **WHEN** a YAML config sets `schedulers[0].extension.name = "capability_aware"` with a matching `typed_config` and `task_type = "echo"`
- **THEN** the resulting message SHALL contain a `SchedulerConfig` with `extension.name = "capability_aware"`, the packed `typed_config`, and `task_type = "echo"`

#### Scenario: Empty task_type marks the default scheduler

- **WHEN** a YAML config sets a `SchedulerConfig` with `extension.name = "round_robin"` and no `task_type`
- **THEN** the resulting `SchedulerConfig` SHALL have an empty `task_type`
- **AND** the gateway SHALL treat that entry as the default scheduler

#### Scenario: TLS fields are reserved for future use

- **WHEN** a future release enables TLS on the gateway
- **THEN** the reserved `TlsConfig tls` field SHALL carry cert/key/ca/verify-peer settings without a breaking schema change
