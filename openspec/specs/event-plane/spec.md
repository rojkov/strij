# event-plane Specification

## Purpose

Defines the CloudEvent event plane: the node-facing publish/subscribe contract, the gateway edge, the backend-agnostic distributed event-plane extension, and the v1 best-effort subscription-summary behavior with its delivery and subscription lifecycle semantics.

## Requirements

### Requirement: CloudEvent envelope and identity

The system SHALL represent workflow events as CloudEvents 1.0. Every event SHALL carry the required context attributes `specversion`, `id`, `source`, and `type`; the context MAY also carry `subject`, `time`, `datacontenttype`, and `dataschema`; the event SHALL carry an opaque `data` payload; and extension attributes MAY be present. An emitted event whose author omits `specversion` SHALL default it to `1.0`; an omitted `id` SHALL be generated; an omitted `time` SHALL be set to the emission time; and JSON data SHALL default `datacontenttype` to `application/json`. The pair `(source, id)` SHALL be the stable event identity. The event payload SHALL be immutable once published.

#### Scenario: Emitted event carries the required attributes

- **WHEN** a workflow emits an event with an authored source, type, and data but no specversion, id, or time
- **THEN** the published event SHALL carry `specversion` `1.0`, a generated `id`, the authored `source` and `type`, an emission `time`, the authored data, and `datacontenttype` `application/json` when the data is JSON

#### Scenario: Authored context attributes are preserved

- **WHEN** a workflow emits an event with explicit id, time, subject, dataschema, and datacontenttype values
- **THEN** the published event SHALL carry those values unchanged

#### Scenario: Event identity is stable

- **WHEN** the same `(source, id)` pair is observed more than once
- **THEN** the pair SHALL identify the same event for deduplication and routing

### Requirement: Node-facing event bus

The nodeagent SHALL expose an event bus to task handlers. The bus SHALL provide publish and subscribe operations. A publish SHALL hand a CloudEvent to a live gateway connection that announced an event plane and return a status; a publish with no live gateway connection SHALL return an unavailable status, and a publish when gateways are live but none announced an event plane SHALL return an unsupported-capability status. A subscribe SHALL take a coarse event pattern, a stable subscription identity, and the `auto` delivery mode; it SHALL complete through a ready callback carrying the actual delivery guarantee, or through an error. The returned subscription handle SHALL provide acknowledge, detach, and delete operations. A subscription SHALL NOT be reported ready before the gateway edge has accepted it.

#### Scenario: Handler publishes an event

- **WHEN** a task handler publishes a CloudEvent and a live gateway connection exists
- **THEN** the event SHALL be written toward that gateway and the publish SHALL return Ok

#### Scenario: Publish without a gateway connection fails

- **WHEN** a task handler publishes a CloudEvent and no live gateway connection exists
- **THEN** the publish SHALL return an unavailable status and the workflow step SHALL be able to report the failure

#### Scenario: Publish without an event plane fails

- **WHEN** a task handler publishes a CloudEvent and live gateways exist but none has an event plane
- **THEN** the publish SHALL return an unsupported-capability status and SHALL NOT silently succeed

#### Scenario: Subscribe reports the actual guarantee

- **WHEN** a task handler subscribes with `auto` on a cluster whose event plane supports only best-effort delivery
- **THEN** the ready callback SHALL report `best-effort`
- **AND** when the event plane supports at-least-once delivery, the ready callback SHALL report `at-least-once`

#### Scenario: Acknowledge is available before durable delivery exists

- **WHEN** a best-effort subscription acknowledges a delivered event
- **THEN** the acknowledgement SHALL succeed as a no-op

#### Scenario: An at-least-once acknowledgement reaches the extension

- **WHEN** a subscription with an at-least-once delivery guarantee acknowledges a delivery
- **THEN** the node SHALL send the opaque token to the gateway edge and the edge SHALL pass it to the event-plane extension's acknowledge operation

### Requirement: Gateway event edge and subscription lifecycle

The gateway SHALL provide an event edge that maps each accepted subscription identity to the node connection that created it and routes event-plane frames between nodes and the event-plane extension. A subscribe arriving on a node connection SHALL register the subscription before the subscription is reported ready. When a node connection is lost, the edge SHALL detach its subscriptions. When a workflow explicitly completes or cancels a subscription, the edge SHALL delete it. Detach SHALL preserve durable subscription state when the event plane supports durability; delete SHALL remove it. A duplicate active subscription identity on the same node SHALL be rejected or replaced deterministically rather than creating two independent subscriptions.

#### Scenario: Subscription routes to its node

- **WHEN** a node subscribes to a pattern and a matching event reaches the gateway
- **THEN** the gateway edge SHALL deliver the event to that node's subscription identity

#### Scenario: Connection loss detaches

- **WHEN** the node connection carrying a subscription is lost
- **THEN** the gateway edge SHALL detach the subscription
- **AND** the node event bus SHALL fail the subscription so a waiting listen reports an unavailable status rather than waiting indefinitely
- **AND** a best-effort event plane SHALL remove it
- **AND** a durable event plane SHALL preserve its identity and offset until the subscription is deleted or expires

#### Scenario: Explicit cancellation deletes

- **WHEN** a workflow completes or cancels a listen subscription
- **THEN** the gateway edge SHALL delete the subscription and stop delivering events to it

#### Scenario: Duplicate subscription identity is deterministic

- **WHEN** a node subscribes twice with the same active subscription identity
- **THEN** the system SHALL reject the duplicate or replace the existing subscription deterministically and SHALL NOT deliver events twice for that identity

### Requirement: Event-plane extension contract

The gateway SHALL provide an event-plane extension category whose contract is backend-agnostic. The contract SHALL support publishing an event, adding, attaching, detaching, and deleting a subscription, acknowledging a delivered event, and delivering an event to a local subscription. The contract SHALL represent delivery offsets and acknowledgement tokens as opaque values. The contract SHALL NOT require a specific broker vocabulary, subject syntax, partition model, or persistence mechanism. The event-plane extension SHALL be the single source of truth for the delivery guarantee: `auto` SHALL resolve to the strongest guarantee the extension supports, and an explicitly requested unsupported guarantee SHALL fail rather than silently degrade.

#### Scenario: Auto resolves through the extension

- **WHEN** a subscription requests `auto`
- **THEN** the extension SHALL select its strongest supported guarantee and report it through the subscription ready result

#### Scenario: Unsupported explicit guarantee fails

- **WHEN** a subscription explicitly requires at-least-once delivery and the configured extension supports only best-effort
- **THEN** the subscription SHALL fail with an unsupported-capability error and SHALL NOT be reported ready

#### Scenario: Backend vocabulary stays out of the contract

- **WHEN** an external-backbone extension and a self-hosted extension implement the same contract
- **THEN** neither SHALL be required to expose its broker-specific subjects, offsets, partitions, or lease representation to the node edge or the workflow interpreter

#### Scenario: Extension owns delivery mode

- **WHEN** the configured event-plane extension changes from a best-effort implementation to a durable implementation
- **THEN** the node edge and the workflow interpreter SHALL NOT require a delivery-mode configuration change

### Requirement: Bundled subscription-summary event plane

The bundled v1 event plane SHALL maintain only the subscriptions local to each gateway and SHALL exchange coarse route summaries with peer gateways. A route summary SHALL describe the exact event `type` values and `source` values that the gateway's local subscriptions can consume, plus a match-all marker for subscriptions without a type or source constraint. The extension SHALL broadcast its summary on change and periodically re-broadcast it. A peer summary SHALL expire after a configured time-to-live without a refresh. On publish, the extension SHALL deliver the event locally when a local subscription matches and SHALL send it to each peer whose summary matches. On receipt, the extension SHALL match the event against its local subscriptions and deliver the full envelope to each match. The extension SHALL NOT evaluate workflow-specific data expressions or correlation rules.

#### Scenario: Summary propagates a new subscription

- **WHEN** a listener subscribes to type `order.created` on one gateway
- **THEN** that gateway's summary SHALL advertise `order.created` to its peers
- **AND** a peer publishing an `order.created` event SHALL forward it to the listener's gateway

#### Scenario: Unrelated peers are skipped

- **WHEN** a peer's summary contains only type `order.paid` and an `order.created` event is published
- **THEN** the event SHALL NOT be sent to that peer

#### Scenario: Match-all subscriptions receive all types

- **WHEN** a local subscription declares no type or source constraint
- **THEN** its gateway's summary SHALL carry the match-all marker
- **AND** every published event SHALL be forwarded to that gateway

#### Scenario: Expired summaries stop traffic

- **WHEN** a peer stops refreshing its summary beyond the configured time-to-live
- **THEN** the publishing gateway SHALL stop sending events to that peer until a fresh summary arrives

#### Scenario: Local subscriptions receive the full envelope

- **WHEN** a local subscription's coarse pattern matches an incoming event
- **THEN** the event plane SHALL deliver the full CloudEvent envelope, not only the data payload

### Requirement: Best-effort delivery semantics

The v1 event plane SHALL provide best-effort, at-most-once delivery. It SHALL NOT persist events, replay events, or guarantee ordering across subscriptions or publishers. An event MAY be lost during summary propagation, a peer failure, a node failure, a network partition, or local queue overflow. The event plane SHALL bound its local delivery and relay queues and SHALL drop events according to a defined policy when a bound is reached. Failure to deliver to one peer or subscription SHALL NOT stop local delivery to other matching subscriptions.

#### Scenario: Summary race may drop an event

- **WHEN** a listener subscribes and an event is published before the listener's summary reaches the publisher
- **THEN** the event MAY be lost and the system SHALL NOT claim at-least-once delivery

#### Scenario: Peer failure does not stop local delivery

- **WHEN** a peer gateway is unreachable and a matching local subscription exists
- **THEN** the local subscription SHALL still receive the event

#### Scenario: Queue overflow drops instead of blocking

- **WHEN** a subscription's delivery queue or a peer's relay queue is full
- **THEN** the event plane SHALL drop an event according to its configured policy, record the drop, and continue serving other subscriptions and peers

#### Scenario: Partition permits local progress

- **WHEN** the gateway mesh is partitioned
- **THEN** each partition SHALL continue delivering events locally and SHALL NOT block on unreachable peers

### Requirement: Subscription identity, delivery tokens, and acknowledgement

A listen subscription SHALL have a stable identity derived from the workflow identity, the step path, and the listen activation index. Every delivery SHALL carry an opaque token. The listener SHALL acknowledge an event after it has finished processing that event, and SHALL acknowledge events that do not satisfy the full workflow-level filter so that a durable implementation can advance its offset. In a best-effort implementation, acknowledgement SHALL be a no-op. The contract SHALL distinguish detach from delete so that an ephemeral implementation can discard a detached subscription while a durable implementation preserves its offset. Lease renewal, retention, and redelivery SHALL be owned by the event-plane extension.

#### Scenario: Subscription identity is stable across reattachment

- **WHEN** the same workflow listen activation reattaches to the event plane
- **THEN** it SHALL use the same subscription identity and SHALL NOT create a second independent subscription

#### Scenario: Non-matching events are acknowledged

- **WHEN** an event matches the subscription's coarse pattern but fails the workflow-level filter
- **THEN** the listener SHALL acknowledge the event and continue waiting

#### Scenario: Detach and delete differ

- **WHEN** a connection is lost and the subscription is later explicitly cancelled
- **THEN** the detach SHALL be distinguishable from the delete in the event-plane contract

#### Scenario: Best-effort acknowledgement has no effect

- **WHEN** a best-effort subscription acknowledges an event
- **THEN** no durable state SHALL be updated

### Requirement: Durable evolution without core changes

The event-plane contract SHALL allow a future extension to provide durable, at-least-once delivery with retained events, durable offsets, redelivery, lease expiry, and bounded time-or-size retention, without changing the node edge or the workflow interpreter. The bundled v1 extension SHALL declare best-effort support only. A future implementation MAY use an external clustered backbone or a self-hosted replicated log; the contract SHALL NOT require either.

#### Scenario: A durable extension can implement the contract

- **WHEN** a durable event-plane extension is configured
- **THEN** it SHALL be able to deliver unacknowledged events after a subscriber reattaches, using the same subscription identity and opaque offset token

#### Scenario: A self-hosted durable extension remains possible

- **WHEN** a future self-hosted replicated log implements the same contract
- **THEN** it SHALL NOT require changes to the workflow interpreter, the node event bus, or the gateway event edge

#### Scenario: The v1 extension declares its limit

- **WHEN** the bundled summary-mesh extension is configured
- **THEN** it SHALL report best-effort support and SHALL NOT claim durable or at-least-once delivery
