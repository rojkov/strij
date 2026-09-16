# gateway-client

## ADDED Requirements

### Requirement: GatewayClient egress facility

The nodeagent SHALL provide a `GatewayClient` egress facility exposed through `NodeagentFactoryContext`. `GatewayClient::Submit(const task::Task& child, gateway::ResultReceiverPtr receiver)` SHALL forward a child task to a gateway by writing an upstream `kTaskSubmission` frame carrying the serialized `Task` (id, type, body, parameters, requirements, deps). The child's outcomes return through the node's `LocalReceiverRegistry` (`child-result-routing`); the `GatewayClient` itself SHALL NOT hold receivers beyond the write/connect lifecycle.

#### Scenario: Submit writes an upstream submission frame

- **WHEN** `Submit(child, receiver)` is called on the `GatewayClient` with a live gateway connection
- **THEN** a `kTaskSubmission` frame SHALL be written to that connection carrying the serialized child task

### Requirement: Live connection reuse

Every accepted gateway connection on the node's `TcpListener` SHALL be registered with the `GatewayClient`. `Submit` SHALL prefer a live registered connection and SHALL select among multiple live connections in FIFO round-robin order. A written-submission connection SHALL NOT be closed by the submit path; the child's result may return on any registered connection.

#### Scenario: Submission reuses a live connection

- **WHEN** the node holds one or more accepted gateway connections and `Submit` is called
- **THEN** the upstream frame SHALL be written on one of the live connections (round-robin order), not on a new dial

#### Scenario: Multiple live connections are load-balanced

- **WHEN** `Submit` is called repeatedly with `N` live connections
- **THEN** consecutive submissions SHALL cycle through all `N` connections before repeating one

### Requirement: GatewayClient configuration

`NodeAgentConfig.gateway_client` SHALL be a dedicated config section declaring the outbound gateway addresses for the forward path. With the section absent or empty, forwarding SHALL still operate over live connections; when additionally no live connection exists, `Submit` SHALL deliver an error to the child's receiver. The section SHALL be additive (existing nodeagent configs remain valid except for the `schedulers` shape change in `nodeagent-config`).

#### Scenario: No addresses and no live connection forwards an error

- **WHEN** `gateway_client.addresses` is empty and the node holds no live gateway connection
- **THEN** a child submitted through the `GatewayClient` SHALL receive an error immediately