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
The system SHALL define constants for TLV type_ids: `kTaskSubmission = 0`, `kResult = 1`, `kHeartbeat = 2`, `kNodeAdvertisement = 3`, `kNodeState = 4`, `kTaskRejected = 5`.

#### Scenario: Type constants are defined
- **WHEN** code references `TlvFrame::kTaskSubmission`, `TlvFrame::kResult`, `TlvFrame::kHeartbeat`, `TlvFrame::kNodeAdvertisement`, `TlvFrame::kNodeState`, or `TlvFrame::kTaskRejected`
- **THEN** they SHALL resolve to 0, 1, 2, 3, 4, and 5 respectively

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
