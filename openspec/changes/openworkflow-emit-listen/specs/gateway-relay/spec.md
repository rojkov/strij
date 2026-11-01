# Spec Delta

## Purpose

Defines the v1 gateway-to-gateway byte transport used by the event plane: the node relay, gateway identity handshake, opaque relay frames, and the complete-bipartite topology assumption, together with the replaceable gateway transport contract.

## ADDED Requirements

### Requirement: Complete-bipartite topology assumption

The v1 gateway relay SHALL rely on the deployment invariant that every gateway is connected to every node. A gateway MAY therefore choose any live node as a relay and expect that node to have a connection to every other gateway. If a relay node lacks a connection to a target gateway, the relay SHALL drop that delivery and SHALL NOT fail the publishing gateway. The system SHALL document the invariant as an operational requirement of the v1 transport.

#### Scenario: Any node can relay to every gateway

- **WHEN** a gateway sends a peer message to any live node under the complete-bipartite invariant
- **THEN** that node SHALL be able to forward the message to every other gateway connection it holds

#### Scenario: A missing target connection drops only that delivery

- **WHEN** a relay node has no connection to one target gateway
- **THEN** the relay SHALL drop that target's copy and SHALL still forward to the remaining targets

### Requirement: Gateway identity handshake

Each gateway SHALL announce a stable identity on every accepted node connection before relay traffic is accepted. The node SHALL record the mapping from gateway identity to that connection and SHALL use it to resolve targeted relay messages. A later handshake for the same identity on the same node SHALL replace the previous mapping deterministically, and the node SHALL NOT deliver the same targeted message to two live connections for the same identity.

#### Scenario: The node learns each gateway identity

- **WHEN** a gateway connects to a node and sends its identity
- **THEN** the node SHALL associate that identity with the connection

#### Scenario: Targeted relay resolves by identity

- **WHEN** a relay message names a target gateway identity
- **THEN** the node SHALL forward the message only to the connection associated with that identity

#### Scenario: A repeated handshake replaces the mapping

- **WHEN** the same gateway identity handshakes again on a different connection to the same node
- **THEN** the node SHALL use the newer mapping for subsequent targeted messages and SHALL NOT duplicate their delivery across both connections

### Requirement: Opaque peer relay frames

The transport SHALL use a relay frame carrying a list of target gateway identities and an opaque payload. An empty target list SHALL mean broadcast to every gateway connection except the sender. The node SHALL forward the payload without interpreting it. A receiving gateway SHALL expose the payload to the gateway transport receiver unchanged. The node SHALL never forward a relay payload back to the connection that sent it.

#### Scenario: Broadcast excludes the sender

- **WHEN** a gateway sends a relay frame with no target identities
- **THEN** the node SHALL forward the payload to every other gateway connection and SHALL NOT echo it to the sender

#### Scenario: Targeted relay reaches only named gateways

- **WHEN** a relay frame names two gateway identities
- **THEN** only those two gateway connections SHALL receive the payload

#### Scenario: The payload is opaque to the node

- **WHEN** the node forwards a relay frame
- **THEN** it SHALL NOT parse, validate, or transform the payload beyond the relay envelope

#### Scenario: Unknown targets are dropped

- **WHEN** a relay frame names a gateway identity the node does not know
- **THEN** the node SHALL drop that target's copy without erroring the whole relay

### Requirement: Gateway transport contract

The gateway SHALL provide a pure-abstract gateway transport contract with send, broadcast, and inbound-message operations. The contract SHALL operate on opaque byte payloads and peer gateway identities. It SHALL provide best-effort, at-most-once delivery and SHALL NOT guarantee ordering, discovery, or peer liveness. The sender identity SHALL be carried inside the event-plane payload rather than supplied by the transport. Inbound callbacks SHALL run on the dispatcher thread and SHALL NOT retain the payload beyond the callback. The contract SHALL define a maximum payload size.

#### Scenario: A transport sends a targeted payload

- **WHEN** the event plane asks the transport to send a payload to a set of peer identities
- **THEN** the transport SHALL attempt delivery to each named peer and SHALL report an immediate delivery error only when it cannot attempt the send

#### Scenario: A transport broadcasts a payload

- **WHEN** the event plane asks the transport to broadcast a payload
- **THEN** the transport SHALL attempt delivery to every peer reachable by that transport

#### Scenario: Inbound payloads are opaque

- **WHEN** a payload arrives from a peer
- **THEN** the transport SHALL pass the bytes to its receiver without parsing them

#### Scenario: The contract does not expose broker concepts

- **WHEN** an alternative transport implements the contract
- **THEN** it SHALL NOT be required to expose subjects, partitions, consumer groups, or connection liveness to the event plane

### Requirement: Relay node selection and failure

The gateway-side relay transport SHALL select a relay node from the gateway's live node directory for each outgoing relay operation. The selection policy SHALL spread load across live nodes and SHALL be deterministic enough to preserve per-publisher ordering when the same publisher and key are used. With no live node, the transport SHALL return an unavailable error and the event plane SHALL drop the best-effort delivery. A relay-node failure SHALL NOT invalidate the event plane or block local delivery.

#### Scenario: No live node yields an error

- **WHEN** the transport is asked to send and the gateway has no live node
- **THEN** the transport SHALL return a non-Ok status and SHALL NOT invent a direct peer connection

#### Scenario: Load is spread across nodes

- **WHEN** a gateway publishes many events while several nodes are live
- **THEN** relay selection SHALL distribute the relay work across those nodes

#### Scenario: Relay failure does not stop local work

- **WHEN** the chosen relay node fails during an operation
- **THEN** that event MAY be lost and the gateway's local event delivery and task execution SHALL continue

### Requirement: Shared node gateway connection pool

The nodeagent SHALL maintain one shared pool of live gateway connections used by the child-forward path and the relay. The existing child-forward facility SHALL become a user of that pool, and the relay SHALL use the same pool to resolve gateway identities and broadcast payloads. The pool SHALL prune dead connections and SHALL NOT expose a closed connection to either user. Child-forward behavior and its round-robin selection SHALL remain unchanged.

#### Scenario: Child forwarding is unchanged

- **WHEN** a child task is forwarded and multiple live gateway connections exist
- **THEN** the existing round-robin child-forward behavior SHALL continue

#### Scenario: Relay resolves from the same pool

- **WHEN** a relay frame names a gateway identity
- **THEN** the node SHALL resolve that identity against the shared pool

#### Scenario: Dead connections are pruned

- **WHEN** a gateway connection closes
- **THEN** both the child-forward path and the relay SHALL stop using it

### Requirement: Replaceable transport

The gateway relay SHALL be one implementation of the gateway transport contract and SHALL be replaceable without changing the event-plane extension, the node event bus, or the workflow interpreter. A future direct gateway mesh or external backbone SHALL be able to provide the same contract or replace the event-plane extension behind the same edge.

#### Scenario: An alternative transport preserves the event plane

- **WHEN** a different transport satisfies the gateway transport contract
- **THEN** the event-plane extension SHALL continue to publish, subscribe, and deliver without modification

#### Scenario: The relay is not required by an external backbone

- **WHEN** an external-backbone event-plane extension is configured
- **THEN** the node relay SHALL NOT be required for correctness

### Requirement: Bounded relay traffic

The relay SHALL enforce a configured maximum payload size and SHALL bound per-connection queued relay traffic. A payload above the limit SHALL be rejected. When a relay queue is full, the relay SHALL drop payloads according to a defined policy and SHALL record the drop rather than growing without bound.

#### Scenario: Oversized relay payload is rejected

- **WHEN** a relay payload exceeds the configured maximum
- **THEN** the relay SHALL reject it and SHALL NOT enqueue it

#### Scenario: A full relay queue drops

- **WHEN** a gateway connection's relay queue is full
- **THEN** the relay SHALL drop according to policy and SHALL NOT block the publishing event loop
