# Typed TLV Messages

## Purpose

Defines the TLV (Type-Length-Value) wire format, frame structure, and parser behavior for typed messages exchanged between gateway and nodeagent components, including the node-plane frame types.

## MODIFIED Requirements

### Requirement: TLV type_id constants
The system SHALL define constants for TLV type_ids: `kTaskSubmission = 0`, `kResult = 1`, `kHeartbeat = 2`, `kNodeAdvertisement = 3`, `kNodeState = 4`, `kTaskRejected = 5`.

#### Scenario: Type constants are defined
- **WHEN** code references `TlvFrame::kTaskSubmission`, `TlvFrame::kResult`, `TlvFrame::kHeartbeat`, `TlvFrame::kNodeAdvertisement`, `TlvFrame::kNodeState`, or `TlvFrame::kTaskRejected`
- **THEN** they SHALL resolve to 0, 1, 2, 3, 4, and 5 respectively

## ADDED Requirements

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
