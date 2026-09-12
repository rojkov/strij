# gateway-client

## Purpose

Defines the nodeagent's outbound gateway path for forwarded child tasks: the `GatewayClient` egress facility implementing the `ChildTaskForwarder` port, live connection reuse and round-robin selection, and the optional `gateway_client` configuration section.

## Requirements

### Requirement: GatewayClient egress facility

The nodeagent SHALL provide a `GatewayClient` egress facility implementing the `ChildTaskForwarder` port and exposed through `NodeagentFactoryContext::ChildTaskForwarder()`. The port SHALL expose `Forward(const task::Task& task) -> absl::Status`, which forwards a child task to a gateway by writing an upstream `kTaskSubmission` frame carrying the serialized `Task` (id, type, body, parameters, requirements, deps). `Forward` SHALL NOT take ownership of or retain the child's result receiver: the receiver stays registered in the node's `LocalResultReceiverStorage` (`child-result-routing`), and the child's outcomes return through that entry. `Forward` SHALL return `OkStatus` once the frame is written (the outcome resolving asynchronously) and a non-Ok status when forwarding is impossible (no live connection registered). The `ChildTaskForwarder` port is consumed only by the node's local-authority scheduler; it is not a handler-facing capability.

#### Scenario: Forward writes an upstream submission frame

- **WHEN** the local-authority scheduler calls `Forward(child)` on the `ChildTaskForwarder` with a live gateway connection
- **THEN** a `kTaskSubmission` frame SHALL be written to that connection carrying the serialized child task

#### Scenario: Forward leaves the receiver in the local storage

- **WHEN** `Forward(child)` is called while the child's receiver is registered in `LocalResultReceiverStorage`
- **THEN** the `ChildTaskForwarder` SHALL NOT take ownership of or retain the receiver
- **AND** the child's outcome SHALL resolve through the registered storage entry

#### Scenario: Forward without a live connection returns an error

- **WHEN** `Forward(child)` is called and no live gateway connection is registered
- **THEN** `Forward` SHALL return a non-Ok status and SHALL NOT retain the child
- **AND** the caller SHALL deliver the error through the child's receiver and erase the storage entry

### Requirement: Live connection reuse

Every accepted gateway connection on the node's `TcpListener` SHALL be registered with the `GatewayClient` via `RegisterConnection`. `Forward` SHALL prefer a live registered connection and SHALL select among multiple live connections in FIFO round-robin order. A connection whose mailbox closes SHALL be unregistered (its slot becomes dead and is pruned on the next `Forward`). A written-submission connection SHALL NOT be closed by the forward path; the child's result may return on any registered connection.

#### Scenario: Submission reuses a live connection

- **WHEN** the node holds one or more accepted gateway connections and `Forward` is called
- **THEN** the upstream frame SHALL be written on one of the live connections (round-robin order), not on a new dial

#### Scenario: Multiple live connections are load-balanced

- **WHEN** `Forward` is called repeatedly with `N` live connections
- **THEN** consecutive forwards SHALL cycle through all `N` connections before repeating one

#### Scenario: A closed connection is dropped from the pool

- **WHEN** a registered connection closes and `Forward` is called afterwards
- **THEN** that connection SHALL NOT receive the frame
- **AND** the forward SHALL use only the still-live registered connections

### Requirement: GatewayClient configuration

`NodeAgentConfig.gateway_client` SHALL be a dedicated config section declaring `repeated string addresses` naming outbound gateway endpoints for the forward path. The section SHALL be optional and additive (existing nodeagent configs remain valid except for the `schedulers` shape change in `nodeagent-config`). With the section absent or empty, forwarding SHALL still operate over live registered connections; when additionally no live connection exists, `Forward` SHALL return a non-Ok status and the caller SHALL resolve the child's receiver with an error. The declared addresses are reserved for a future outbound dial fallback and SHALL NOT be required for the forward path.

#### Scenario: No addresses and no live connection forwards an error

- **WHEN** `gateway_client.addresses` is empty and the node holds no live gateway connection
- **THEN** `Forward` SHALL return a non-Ok status
- **AND** the caller SHALL deliver an error through the child's receiver immediately
