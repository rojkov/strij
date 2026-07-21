## ADDED Requirements

### Requirement: TlvFrame struct
The system SHALL define a `TlvFrame` struct containing `uint8_t type_id` and `std::span<const std::byte> value` representing the frame's value payload.

#### Scenario: TlvFrame holds parsed TLV data
- **WHEN** a TLV frame is parsed from the wire
- **THEN** the resulting `TlvFrame` SHALL contain the type_id byte and a span over the value bytes

### Requirement: TLV wire format
The TLV wire format SHALL encode `[type_id: 1 byte][length: 4 bytes][value: N bytes]`. The value content is type-specific: task-related frames (TaskSubmission, Result) carry `[task_id: 8 bytes][payload: N bytes]` in the value. Heartbeat frames carry no task_id.

#### Scenario: Task submission frame on the wire
- **WHEN** gateway sends a task submission to nodeagent
- **THEN** the TLV frame on the wire SHALL contain type_id=0 (TaskSubmission), length=8+payload_size, and a value of 8 bytes of task_id followed by the task payload

#### Scenario: Task result frame on the wire
- **WHEN** nodeagent sends a task result back to gateway
- **THEN** the TLV frame on the wire SHALL contain type_id=1 (Result), length=8+payload_size, and a value of 8 bytes of task_id followed by the result payload

#### Scenario: Heartbeat frame on the wire
- **WHEN** either side sends a heartbeat
- **THEN** the TLV frame on the wire SHALL contain type_id=2 (Heartbeat) and an empty or type-specific value with no task_id

### Requirement: TlvParser delivers TlvFrame
`TlvParser` SHALL deliver `TlvFrame` structs to its callback instead of raw byte spans. The callback type SHALL be `std::function<void(TlvFrame)>`. The parser SHALL set `type_id` from the type byte and `value` from the value bytes. The parser SHALL NOT interpret the value content — task_id extraction is the consumer's responsibility.

#### Scenario: Parser delivers a complete frame
- **WHEN** `TlvParser::OnData` processes a complete TLV frame
- **THEN** it SHALL invoke the callback with a `TlvFrame` containing type_id and the full value span

### Requirement: TLV type_id constants
The system SHALL define constants for TLV type_ids: `kTaskSubmission = 0`, `kResult = 1`, `kHeartbeat = 2`.

#### Scenario: Type constants are defined
- **WHEN** code references `TlvFrame::kTaskSubmission`, `TlvFrame::kResult`, or `TlvFrame::kHeartbeat`
- **THEN** they SHALL resolve to 0, 1, and 2 respectively
