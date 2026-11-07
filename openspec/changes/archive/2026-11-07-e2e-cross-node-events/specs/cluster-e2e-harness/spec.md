# Spec Delta

## MODIFIED Requirements

### Requirement: Two-phase cluster boot

The harness SHALL start every node and wait until each node's TLV listener accepts a connection
before starting any gateway, because a gateway that fails its first connection attempt never
re-dials that node. It SHALL then start gateways one at a time and prove each gateway ready before
spawning the next, so each node's gateway-connection order is deterministic.

#### Scenario: Nodes are listening before gateways dial

- **GIVEN** a cluster specification with N nodes and M gateways
- **WHEN** the cluster is started
- **THEN** all N node listeners accept connections before the first gateway process is spawned
- **AND** no gateway reports a failed connection to a node address

#### Scenario: A gateway started before a node stays permanently blind

- **GIVEN** a node whose listener is not yet bound
- **WHEN** a gateway dials that node and the connection is refused
- **THEN** the gateway never re-dials that address for the rest of the process lifetime
- **AND** tasks submitted to that gateway report `CAPACITY_REFUSED` rather than waiting
- **AND** the harness documents this as the reason its boot order is fixed

#### Scenario: Gateways start one at a time

- **GIVEN** a cluster specification with multiple gateways
- **WHEN** the cluster is started
- **THEN** gateway `j` is spawned only after gateway `j-1` has served a marker task from every node
- **AND** each node registers its gateway connections in gateway start order
- **AND** the resulting event-capable connection order at every node matches the gateway start order

## ADDED Requirements

### Requirement: Event-plane configuration is generated on request

The harness SHALL generate a gateway's optional `event_plane` section from the cluster
specification, preserving the extension name, typed config, and gateway id, so a scenario can start
real gateways with the bundled `summary_mesh` plane. A specification without an event plane SHALL
produce a gateway configuration without that section.

#### Scenario: A configured event plane is written and round-trips

- **GIVEN** a gateway specification naming `summary_mesh`, a `SummaryMeshConfig`, and a gateway id
- **WHEN** the harness writes the gateway configuration
- **THEN** the generated YAML contains an `event_plane` section with the extension name, typed
  config, and gateway id
- **AND** `LoadConfig<GatewayConfig>` loads it with `has_event_plane()` true and the gateway id
  preserved
- **AND** the typed config unpacks to the configured `SummaryMeshConfig`

#### Scenario: No event plane is written when not specified

- **GIVEN** a gateway specification with no event plane
- **WHEN** the harness writes the gateway configuration
- **THEN** the generated YAML contains no `event_plane` section
- **AND** `LoadConfig<GatewayConfig>` reports `has_event_plane()` false

### Requirement: Cross-node event delivery is proven through real processes

The harness SHALL support an end-to-end scenario in which a workflow on one node suspends in
`listen` and a workflow on another node publishes a matching CloudEvent, with both workflows
submitted through the gateway HTTP listeners. The scenario SHALL place the listener and emitter on
different nodes and SHALL assert the listener's response identifies the consuming node and the
event's producing node.

#### Scenario: A listener on one node consumes an emitter on another

- **GIVEN** a ready cluster with two nodes and at least one gateway with an event plane
- **AND** a listener workflow on one node whose `listen` step waits for
  `com.example.order.created`
- **AND** an emitter workflow on the other node that emits that type with data naming its own node
- **WHEN** the emitter's event is published while the listener is waiting
- **THEN** the listener's HTTP request completes with status 200
- **AND** the response identifies the listener's node as the consumer
- **AND** the response's event data identifies the emitter's node as the producer
- **AND** the emitter's own HTTP response identifies its node

#### Scenario: The event crosses gateways through a relay node

- **GIVEN** two event-plane gateways started in order and connected to both nodes
- **WHEN** the listener subscribes through the first-started gateway and the matching event is
  published to the second gateway
- **THEN** the second gateway relays the event through a live node
- **AND** the first gateway delivers it to the listening node
- **AND** the listener completes even though no gateway hosts both the subscription and the publish
  locally

#### Scenario: Workflows are pinned to distinct nodes without narrowing discovery

- **GIVEN** two nodes that each declare a distinct resource pool
- **AND** gateways that route `openworkflow` by the task's resource requirements
- **WHEN** the listener and emitter are submitted with their respective pool requirements
- **THEN** the listener runs only on the node declaring its pool
- **AND** the emitter runs only on the node declaring its pool
- **AND** every gateway still discovers and serves both nodes during readiness

#### Scenario: Best-effort retries are bounded

- **GIVEN** the bundled `summary_mesh` plane is best-effort and may drop an event published before
  the subscription summary reaches the publishing gateway
- **WHEN** the listener has not completed yet
- **THEN** the test re-submits the emitter workflow until the listener completes or a bounded
  deadline expires
- **AND** a deadline expiry fails the test with the cluster diagnostics
- **AND** the listener request itself remains bounded by its own HTTP timeout
