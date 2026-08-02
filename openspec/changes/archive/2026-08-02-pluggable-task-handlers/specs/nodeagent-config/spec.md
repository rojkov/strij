# NodeAgent Config Capability

## Purpose

Protobuf schema and default configuration for the Carrot NodeAgent service: defines all nodeagent-configurable parameters, their validation constraints and default values, plus cross-field rules such as heartbeat interval validation against the connection timeout.

## MODIFIED Requirements

### Requirement: NodeAgentConfig protobuf schema

The system SHALL define a `NodeAgentConfig` protobuf message in `api/core/config/nodeagent.proto` (package `carrot.config`) with `TlvListener tlv_listener`, `Logging logging`, `repeated ExtensionConfig task_handlers`, and reserved-for-future fields `connection_timeout`, `heartbeat_interval`, and `TlsConfig tls`.

#### Scenario: Listener and logging sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with `tlv_listener` and `logging` sections
- **THEN** the resulting message SHALL contain the corresponding `TlvListener` and `Logging` values

#### Scenario: Task handler sections load into the message

- **WHEN** a `NodeAgentConfig` is loaded from YAML with a `task_handlers` list containing entries with `name` and `typed_config`
- **THEN** the resulting message SHALL contain each `ExtensionConfig` entry with its `name` and packed `typed_config`

#### Scenario: TLS fields are reserved for future use

- **WHEN** a future release enables TLS on the nodeagent
- **THEN** the reserved `TlsConfig tls` field SHALL carry cert/key/ca/verify-peer settings without a breaking schema change
