# Typed TLV Messages

## Purpose

Defines the TLV (Type-Length-Value) wire format, frame structure, and parser behavior for typed messages exchanged between gateway and nodeagent components, including the node-plane frame types.

## Requirements

### Requirement: TlvFrame struct
The system SHALL define a `TlvFrame` struct containing `uint8_t type_id` and `std::span<const std::byte> value` representing the frame's value payload.

#### Scenario: TlvFrame holds parsed TLV data
- **WHEN** a TLV frame is parsed from the wire
- **THEN** the resulting `TlvFrame` SHALL contain the type_id byte and a span over the value bytes

### Requirement: TLV wire format
The TLV wire format SHALL encode `[type_id: 1 byte][length: 4 bytes][value: N bytes]`. The value content is type-specific: task-related frames carry the serialized protobuf message `Task` (TaskSubmission) or `TaskResult` (Result). Heartbeat frames carry no task payload.

#### Scenario: Task submission frame on the wire
- **WHEN** gateway sends a task submission to nodeagent
- **THEN** the TLV frame on the wire SHALL contain type_id=0 (TaskSubmission), length equal to the serialized `Task` size, and a value of the serialized `Task` protobuf message

#### Scenario: Task result frame on the wire
- **WHEN** nodeagent sends a task result back to gateway
- **THEN** the TLV frame on the wire SHALL contain type_id=1 (Result), length equal to the serialized `TaskResult` size, and a value of the serialized `TaskResult` protobuf message

#### Scenario: Heartbeat frame on the wire
- **WHEN** either side sends a heartbeat
- **THEN** the TLV frame on the wire SHALL contain type_id=2 (Heartbeat) and an empty or type-specific value with no task payload

### Requirement: TlvParser delivers TlvFrame
`TlvParser` SHALL deliver `TlvFrame` structs to its callback instead of raw byte spans. The callback type SHALL be `std::function<void(TlvFrame)>`. The parser SHALL set `type_id` from the type byte and `value` from the value bytes. The parser SHALL NOT interpret the value content — protobuf parsing of the value is the consumer's responsibility.

#### Scenario: Parser delivers a complete frame
- **WHEN** `TlvParser::OnData` processes a complete TLV frame
- **THEN** it SHALL invoke the callback with a `TlvFrame` containing type_id and the full value span

### Requirement: TLV type_id constants
The system SHALL define constants for TLV type_ids: `kTaskSubmission = 0`, `kResult = 1`, `kHeartbeat = 2`, `kNodeAdvertisement = 3`, `kNodeState = 4`, `kTaskRejected = 5`, `kTaskProbe = 6`, `kTaskProbeCancel = 7`, `kTaskPull = 8`, `kTaskGrant = 9`, `kTaskDecline = 10`. Existing constants (0–5) SHALL remain unchanged and unrenumbered; endpoints not implementing the probe protocol SHALL drop frames of probe-protocol ids (6–10) as unknown.

#### Scenario: Type constants are defined
- **WHEN** code references `TlvFrame::kTaskSubmission`, `TlvFrame::kResult`, `TlvFrame::kHeartbeat`, `TlvFrame::kNodeAdvertisement`, `TlvFrame::kNodeState`, or `TlvFrame::kTaskRejected`
- **THEN** they SHALL resolve to 0, 1, 2, 3, 4, and 5 respectively

#### Scenario: Probe scheduling constants are defined
- **WHEN** code references `TlvFrame::kTaskProbe`, `TlvFrame::kTaskProbeCancel`, `TlvFrame::kTaskPull`, `TlvFrame::kTaskGrant`, and `TlvFrame::kTaskDecline`
- **THEN** they SHALL resolve to 6, 7, 8, 9, and 10 respectively

### Requirement: TLV wire format serialization
The system SHALL provide a `SerializeTlvFrame(uint8_t type_id, std::span<const std::byte> value) -> std::vector<std::byte>` free function that serializes a TLV frame into the wire format `[type_id:1][length:4 network-order][value:N]`. The caller is responsible for composing the value content (e.g., serializing a `Task` or `TaskResult` protobuf message into the value span).

#### Scenario: Serialize a task submission frame
- **WHEN** `SerializeTlvFrame(kTaskSubmission, value)` is called with a non-empty value span containing a serialized `Task`
- **THEN** it SHALL return a byte vector containing type_id=0, length in network byte order equal to value.size(), followed by the value bytes

#### Scenario: Serialize an empty value
- **WHEN** `SerializeTlvFrame(kHeartbeat, span{})` is called
- **THEN** it SHALL return a byte vector with type_id=2, length=0, and no value bytes

#### Scenario: Serialize preserves wire format compatibility
- **WHEN** `SerializeTlvFrame(type_id, value)` is called
- **THEN** the resulting byte vector SHALL be parseable by `TlvParser` to produce a `TlvFrame` with matching type_id and value

### Requirement: Node-plane TLV frame payloads
`kNodeAdvertisement` SHALL carry a serialized `strij.node.NodeCapabilities` message, `kNodeState` SHALL carry a serialized `strij.node.NodeState` message, and `kTaskRejected` SHALL carry a serialized `strij.task.TaskRejected` message.

#### Scenario: Advertisement frame carries NodeCapabilities
- **WHEN** a nodeagent serializes a `kNodeAdvertisement` frame whose value is a serialized `NodeCapabilities`
- **THEN** a consumer parsing the frame value SHALL recover the original `NodeCapabilities`

#### Scenario: State frame carries NodeState
- **WHEN** a nodeagent serializes a `kNodeState` frame whose value is a serialized `NodeState`
- **THEN** a consumer parsing the frame value SHALL recover the original `NodeState`

#### Scenario: Rejection frame carries TaskRejected
- **WHEN** a nodeagent serializes a `kTaskRejected` frame whose value is a serialized `TaskRejected`
- **THEN** a consumer parsing the frame value SHALL recover the original `TaskRejected`

### Requirement: Probe scheduling TLV frame payloads
`kTaskProbe` SHALL carry a serialized `strij.task.TaskProbe` message, `kTaskPull` a serialized `strij.task.TaskPull`, `kTaskProbeCancel` a serialized `strij.task.TaskProbeCancel`, `kTaskDecline` a serialized `strij.task.TaskDecline`, and `kTaskGrant` a serialized `strij.task.Task` message (the full task, body included).

#### Scenario: Probe frame carries TaskProbe

- **WHEN** a gateway serializes a `kTaskProbe` frame whose value is a serialized `TaskProbe`
- **THEN** a consumer parsing the frame value SHALL recover the original `TaskProbe`

#### Scenario: Grant frame carries the full Task

- **WHEN** a gateway serializes a `kTaskGrant` frame whose value is a serialized `Task`
- **THEN** a consumer parsing the frame value SHALL recover the original `Task` including its body

### Requirement: Event-plane and gateway-relay frame type IDs

The system SHALL define append-only TLV type_ids `kGatewayHello = 11`, `kPeerRelay = 12`, `kPeerRelayDelivery = 13`, `kEventPublish = 14`, `kEventSubscribe = 15`, `kEventUnsubscribe = 16`, `kEventDelivery = 17`, `kEventAck = 18`, and `kEventSubscribeResult = 19`. The existing constants 0 through 10 SHALL remain unchanged and SHALL NOT be renumbered. An endpoint that does not implement the event plane SHALL drop these frames as unknown.

#### Scenario: New frame constants are defined

- **WHEN** code references the event-plane and relay constants
- **THEN** `kGatewayHello`, `kPeerRelay`, `kPeerRelayDelivery`, `kEventPublish`, `kEventSubscribe`, `kEventUnsubscribe`, `kEventDelivery`, `kEventAck`, and `kEventSubscribeResult` SHALL resolve to 11, 12, 13, 14, 15, 16, 17, 18, and 19 respectively

#### Scenario: Existing frame numbering is unchanged

- **WHEN** the event-plane frames are added
- **THEN** `kTaskSubmission` through `kTaskDecline` SHALL keep their existing values 0 through 10

#### Scenario: An unaware endpoint drops the new frames

- **WHEN** a component that does not implement the event plane receives one of the new frame types
- **THEN** it SHALL drop the frame as unknown and SHALL NOT reinterpret it as an existing frame type

### Requirement: Gateway-relay frame payloads

`kGatewayHello` SHALL carry a serialized gateway identity message containing a non-empty gateway id and the strongest delivery mode the gateway's event plane supports; its presence SHALL also tell the node that the gateway has an event plane. `kPeerRelay` SHALL carry a serialized relay message containing zero or more target gateway ids and an opaque payload; an empty target list SHALL mean broadcast to every other gateway connection. `kPeerRelayDelivery` SHALL carry a serialized delivery message containing the opaque payload. The relay frames SHALL NOT require the node to interpret the event-plane payload.

#### Scenario: Gateway hello carries the identity

- **WHEN** a gateway serializes `kGatewayHello` with its gateway id
- **THEN** the node SHALL recover the same non-empty gateway id and delivery mode

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

`kEventPublish` SHALL carry a serialized CloudEvent. `kEventSubscribe` SHALL carry a serialized subscription request containing a stable subscription id, a coarse pattern with optional type, source, and subject, and the `auto` delivery mode. `kEventUnsubscribe` SHALL carry the subscription id being removed. `kEventDelivery` SHALL carry the subscription id, an opaque delivery token, and the serialized CloudEvent envelope. `kEventAck` SHALL carry the subscription id and the opaque delivery token being acknowledged. `kEventSubscribeResult` SHALL carry the subscription id, whether the subscription was accepted, a rejection reason, and the actual delivery mode resolved by the extension. The event-plane frames SHALL be interpreted only by the node event bus and the gateway event edge, never by a scheduler.

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

#### Scenario: Subscribe result reports the actual mode

- **WHEN** a gateway accepts a subscription request
- **THEN** it SHALL send a `kEventSubscribeResult` carrying the subscription id, acceptance, and the actual delivery mode

#### Scenario: Acknowledgement carries the opaque token

- **WHEN** a node acknowledges a delivery
- **THEN** the gateway SHALL recover the subscription id and the opaque token from `kEventAck`

#### Scenario: Event frames are not scheduler frames

- **WHEN** an event-plane frame reaches the gateway or node TLV handler
- **THEN** it SHALL be handled by the event edge and SHALL NOT be dispatched to a scheduler as a scheduling protocol frame
