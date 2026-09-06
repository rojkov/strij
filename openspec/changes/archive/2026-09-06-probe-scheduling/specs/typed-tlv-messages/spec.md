## ADDED Requirements

### Requirement: Probe scheduling TLV constants

The system SHALL define additional TLV type_id constants for the probe scheduling protocol: `kTaskProbe = 6` (gateway→node), `kTaskProbeCancel = 7` (gateway→node), `kTaskPull = 8` (node→gateway), `kTaskGrant = 9` (gateway→node), and `kTaskDecline = 10` (node→gateway). Existing constants (0–5) SHALL remain unchanged and unrenumbered; endpoints not implementing the probe protocol SHALL drop frames of these ids as unknown.

#### Scenario: Probe constants are defined

- **WHEN** code references `TlvFrame::kTaskProbe`, `TlvFrame::kTaskProbeCancel`, `TlvFrame::kTaskPull`, `TlvFrame::kTaskGrant`, and `TlvFrame::kTaskDecline`
- **THEN** they SHALL resolve to 6, 7, 8, 9, and 10 respectively

#### Scenario: Existing constants are unchanged

- **WHEN** code references `TlvFrame::kTaskSubmission` through `TlvFrame::kTaskRejected`
- **THEN** they SHALL still resolve to 0 through 5

### Requirement: Probe frame payloads

`kTaskProbe` SHALL carry a serialized `strij.task.TaskProbe` message, `kTaskPull` a serialized `strij.task.TaskPull`, `kTaskProbeCancel` a serialized `strij.task.TaskProbeCancel`, `kTaskDecline` a serialized `strij.task.TaskDecline`, and `kTaskGrant` a serialized `strij.task.Task` message (the full task, body included).

#### Scenario: Probe frame carries TaskProbe

- **WHEN** a nodeagent serializes a `kTaskProbe` frame whose value is a serialized `TaskProbe`
- **THEN** a consumer parsing the frame value SHALL recover the original `TaskProbe`

#### Scenario: Grant frame carries the full Task

- **WHEN** a gateway serializes a `kTaskGrant` frame whose value is a serialized `Task`
- **THEN** a consumer parsing the frame value SHALL recover the original `Task` including its body