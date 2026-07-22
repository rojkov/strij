## MODIFIED Requirements

### Requirement: TLV wire format serialization
The system SHALL provide a `SerializeTlvFrame(uint8_t type_id, std::span<const std::byte> value) -> std::vector<std::byte>` free function that serializes a TLV frame into the wire format `[type_id:1][length:4 network-order][value:N]`. The caller is responsible for composing the value content (e.g., prepending task_id to payload).

#### Scenario: Serialize a task submission frame
- **WHEN** `SerializeTlvFrame(kTaskSubmission, value)` is called with a non-empty value span
- **THEN** it SHALL return a byte vector containing type_id=0, length in network byte order equal to value.size(), followed by the value bytes

#### Scenario: Serialize an empty value
- **WHEN** `SerializeTlvFrame(kHeartbeat, span{})` is called
- **THEN** it SHALL return a byte vector with type_id=2, length=0, and no value bytes

#### Scenario: Serialize preserves wire format compatibility
- **WHEN** `SerializeTlvFrame(type_id, value)` is called
- **THEN** the resulting byte vector SHALL be parseable by `TlvParser` to produce a `TlvFrame` with matching type_id and value
