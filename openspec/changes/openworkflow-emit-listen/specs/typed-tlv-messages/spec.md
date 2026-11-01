# Spec Delta

## ADDED Requirements

### Requirement: Event-plane and gateway-relay frame type IDs

The system SHALL define append-only TLV type_ids `kGatewayHello = 11`, `kPeerRelay = 12`, `kPeerRelayDelivery = 13`, `kEventPublish = 14`, `kEventSubscribe = 15`, `kEventUnsubscribe = 16`, and `kEventDelivery = 17`. The existing constants 0 through 10 SHALL remain unchanged and SHALL NOT be renumbered. An endpoint that does not implement the event plane SHALL drop these frames as unknown.

#### Scenario: New frame constants are defined

- **WHEN** code references the event-plane and relay constants
- **THEN** `kGatewayHello`, `kPeerRelay`, `kPeerRelayDelivery`, `kEventPublish`, `kEventSubscribe`, `kEventUnsubscribe`, and `kEventDelivery` SHALL resolve to 11, 12, 13, 14, 15, 16, and 17 respectively

#### Scenario: Existing frame numbering is unchanged

- **WHEN** the event-plane frames are added
- **THEN** `kTaskSubmission` through `kTaskDecline` SHALL keep their existing values 0 through 10

#### Scenario: An unaware endpoint drops the new frames

- **WHEN** a component that does not implement the event plane receives one of the new frame types
- **THEN** it SHALL drop the frame as unknown and SHALL NOT reinterpret it as an existing frame type

### Requirement: Gateway-relay frame payloads

`kGatewayHello` SHALL carry a serialized gateway identity message containing a non-empty gateway id. `kPeerRelay` SHALL carry a serialized relay message containing zero or more target gateway ids and an opaque payload; an empty target list SHALL mean broadcast to every other gateway connection. `kPeerRelayDelivery` SHALL carry a serialized delivery message containing the opaque payload. The relay frames SHALL NOT require the node to interpret the event-plane payload.

#### Scenario: Gateway hello carries the identity

- **WHEN** a gateway serializes `kGatewayHello` with its gateway id
- **THEN** the node SHALL recover the same non-empty gateway id

#### Scenario: Relay frame carries targets and an opaque payload

- **WHEN** a gateway serializes `kPeerRelay` with two target ids and a payload
- **THEN** the node SHALL recover both target ids and the identical payload bytes

#### Scenario: An empty target list broadcasts

- **WHEN** a `kPeerRelay` frame carries no target ids
- **THEN** the node SHALL treat it as a broadcast to every other gateway connection

#### Scenario: Relay delivery carries the payload unchanged

- **WHEN** a node forwards a relay payload to a gateway
- **THEN** the receiving gateway SHALL recover the identical payload bytes from `kPeerRelayDelivery`

### Requirement: Event-plane frame payloads

`kEventPublish` SHALL carry a serialized CloudEvent. `kEventSubscribe` SHALL carry a serialized subscription request containing a stable subscription id, a coarse pattern with optional type, source, and subject, and the `auto` delivery mode. `kEventUnsubscribe` SHALL carry the subscription id being removed. `kEventDelivery` SHALL carry the subscription id, an opaque delivery token, and the serialized CloudEvent envelope. The event-plane frames SHALL be interpreted only by the node event bus and the gateway event edge, never by a scheduler.

#### Scenario: Publish frame carries a CloudEvent

- **WHEN** a node serializes a `kEventPublish` frame with a CloudEvent
- **THEN** the gateway SHALL recover the same CloudEvent envelope

#### Scenario: Subscribe frame carries the coarse pattern

- **WHEN** a node subscribes with a subscription id, a type, a source, and `auto`
- **THEN** the gateway SHALL recover the same subscription id, pattern, and delivery mode

#### Scenario: Delivery frame carries the token and envelope

- **WHEN** the gateway delivers an event to a node subscription
- **THEN** the node SHALL recover the subscription id, the delivery token, and the full CloudEvent envelope

#### Scenario: Unsubscribe identifies the subscription

- **WHEN** a node cancels a subscription
- **THEN** the gateway SHALL recover the subscription id from `kEventUnsubscribe`

#### Scenario: Event frames are not scheduler frames

- **WHEN** an event-plane frame reaches the gateway or node TLV handler
- **THEN** it SHALL be handled by the event edge and SHALL NOT be dispatched to a scheduler as a scheduling protocol frame
