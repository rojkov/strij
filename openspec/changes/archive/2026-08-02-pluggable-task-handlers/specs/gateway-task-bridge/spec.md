# Gateway Task Bridge

## Purpose

Bridges HTTP requests at the gateway to nodeagent task processing via TLV connections, managing task lifecycle from submission through result delivery.

## MODIFIED Requirements

### Requirement: NodeagentTlvHandler echoes task bodies
`NodeagentTlvHandler` SHALL receive `TlvFrame` messages, and for type_id=TaskSubmission, parse the value as a `Task` protobuf message, look up a `TaskHandler` for `task.type()` in its shared `TaskHandlerManager`, and delegate processing. The task handler delivers `TaskResult` messages back to the originating connection through a `ResultSender` bound to that connection. Malformed frames and tasks whose type has no registered handler SHALL be logged and discarded.

#### Scenario: Nodeagent routes a task to a registered handler
- **WHEN** a `TlvFrame` with type_id=TaskSubmission arrives at the nodeagent and `task.type()` matches a registered task handler
- **THEN** the handler SHALL parse the value as a `Task` protobuf message
- **AND** request the task handler for `task.type()` from `TaskHandlerManager`
- **AND** delegate the task to that handler with a `ResultSender` bound to the originating connection

#### Scenario: Malformed task frame
- **WHEN** a `TlvFrame` with type_id=TaskSubmission arrives and its value does not parse as a `Task`
- **THEN** the handler SHALL log a warning and discard the frame

#### Scenario: Task with no registered handler
- **WHEN** a `TlvFrame` with type_id=TaskSubmission arrives and `task.type()` has no registered task handler
- **THEN** the handler SHALL log a warning and discard the frame
