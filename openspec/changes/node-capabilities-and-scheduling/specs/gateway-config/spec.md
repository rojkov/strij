# Gateway Config Capability

## Purpose

Protobuf schema and default configuration for the Strij Gateway service: defines all gateway-configurable parameters, their validation constraints and default values, plus cross-field rules such as requiring the `node_discovery` extension and defaulting the scheduler.

## MODIFIED Requirements

### Requirement: GatewayConfig protobuf schema

The system SHALL define a `GatewayConfig` protobuf message in `api/core/config/gateway.proto` (package `strij.config`) with `HttpListener http_listener`, `repeated NodeConnection node_connections`, `Logging logging`, `ExtensionConfig node_discovery`, `ExtensionConfig scheduler`, and reserved-for-future fields `connection_timeout`, `request_timeout`, and `TlsConfig tls`.

#### Scenario: Listener and logging sections load into the message

- **WHEN** a `GatewayConfig` is loaded from YAML with `http_listener`, `logging`, `node_discovery`, and `scheduler` sections
- **THEN** the resulting message SHALL contain the corresponding `HttpListener`, `Logging`, `node_discovery`, and `scheduler` values

#### Scenario: Scheduler loads as an ExtensionConfig

- **WHEN** a YAML config sets `scheduler.name = "capability_aware"` with a matching `typed_config`
- **THEN** the resulting message SHALL contain an `ExtensionConfig` with `name = "capability_aware"` and the packed `typed_config`

#### Scenario: TLS fields are reserved for future use

- **WHEN** a future release enables TLS on the gateway
- **THEN** the reserved `TlsConfig tls` field SHALL carry cert/key/ca/verify-peer settings without a breaking schema change
