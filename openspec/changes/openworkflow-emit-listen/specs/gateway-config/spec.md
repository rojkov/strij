# Spec Delta

## ADDED Requirements

### Requirement: Event-plane extension configuration

`GatewayConfig` SHALL define an optional `EventPlaneConfig event_plane` section containing an `ExtensionConfig extension` and a `string gateway_id`. When the section is absent, the gateway SHALL run without an event plane: task execution, scheduling, and node management SHALL continue, while `emit` and `listen` steps SHALL fail with an unsupported-capability status. When the section is present, the gateway SHALL require a non-empty extension name, resolve it in `Registry<EventPlaneFactory>`, unpack its typed config, and construct the extension; an unknown name or unpack failure SHALL fail gateway startup. The `gateway_id` SHALL be the identity announced in the relay handshake. When `gateway_id` is empty, the gateway SHALL generate a process-unique identity. When it is set, the operator SHALL keep it unique across the cluster; a duplicate identity SHALL resolve deterministically at the node relay.

#### Scenario: Event plane loads from configuration

- **WHEN** `event_plane.extension.name` names a registered event-plane factory and its typed config unpacks
- **THEN** the gateway SHALL construct that extension and use it for event-plane operations

#### Scenario: Unknown event-plane extension fails startup

- **WHEN** `event_plane.extension.name` does not resolve to a registered factory, or its typed config does not unpack
- **THEN** the gateway SHALL fail to start with an error naming the extension

#### Scenario: Absent event plane leaves other features working

- **WHEN** `GatewayConfig.event_plane` is absent
- **THEN** the gateway SHALL still serve tasks and manage nodes
- **AND** an `emit` or `listen` step SHALL fail with an unsupported-capability status rather than hanging

#### Scenario: Missing gateway identity is generated

- **WHEN** `event_plane` is present and `gateway_id` is empty
- **THEN** the gateway SHALL generate a non-empty identity for the relay handshake

#### Scenario: Configured gateway identity is used

- **WHEN** `event_plane.gateway_id` is set to a non-empty value
- **THEN** the relay handshake SHALL announce that value
