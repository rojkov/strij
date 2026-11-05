# Spec Delta

## ADDED Requirements

### Requirement: Gateway event-plane extension category

The gateway SHALL have an event-plane extension category. Its public factory and contract SHALL be pure-abstract types under `include/strij/`, and its concrete implementation SHALL remain under `src/`. The factory SHALL be registered in `Registry<EventPlaneFactory>` and loaded from `GatewayConfig.event_plane`. The factory's `Create` SHALL receive the existing `GatewayFactoryContext&` and an additional event-plane dependency bundle containing the dispatcher, the gateway identity, and the gateway transport. The event-plane dependency bundle SHALL NOT widen `GatewayFactoryContext` for scheduler or node-discovery extensions. The `GatewayTransport` contract and the node `EventBus` contract SHALL be public pure-abstract contracts; their concrete node-relay and node-client implementations SHALL NOT be visible to extension authors.

#### Scenario: A third-party event-plane extension is registered

- **WHEN** an external repository implements `EventPlaneFactory`, registers it with `REGISTER_FACTORY`, and links it through the gateway composition macro
- **THEN** selecting that name in `GatewayConfig.event_plane` SHALL construct the extension without modifying the Strij source tree

#### Scenario: Event-plane dependencies are category-specific

- **WHEN** a scheduler or node-discovery factory receives its gateway factory context
- **THEN** it SHALL NOT see the event-plane dependency bundle or its transport

#### Scenario: External and self-hosted implementations share the contract

- **WHEN** an external-backbone extension and a self-hosted replicated-log extension both implement the event-plane contract
- **THEN** both SHALL be usable through the same public factory interface without changing the node edge or the workflow interpreter

#### Scenario: Transport stays abstract

- **WHEN** an event-plane extension uses the gateway transport contract
- **THEN** it SHALL depend only on the public pure-abstract contract and SHALL NOT depend on the node-relay implementation
