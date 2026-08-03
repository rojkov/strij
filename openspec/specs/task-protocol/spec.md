# task-protocol

## Purpose

Defines the Protobuf message schemas used to carry tasks and results between gateway and nodeagent over TLV frames, giving tasks a structured, self-describing representation with `id`, `type`, and `body` fields.

## Requirements

### Requirement: Task message schema
The system SHALL define a Protobuf message `Task` in package `strij.task` with fields `string id = 1`, `string type = 2`, and `bytes body = 3`. The `id` SHALL be a human-readable, randomly generated string identifier (e.g., `happy_fox_runs_k7m2x9p4`) used to route results back to the originating HTTP client. The `type` SHALL identify the task handler intended to process the task. The `body` SHALL be the task payload.

#### Scenario: Task carries string id, type, and body
- **WHEN** a task with id="happy_fox_runs_k7m2x9p4", type="echo", and body "hello" is serialized
- **THEN** the serialized bytes SHALL parse back into a `Task` with id="happy_fox_runs_k7m2x9p4", type="echo", and body "hello"

### Requirement: TaskResult message schema
The system SHALL define a Protobuf message `TaskResult` in package `strij.task` with fields `string id = 1` and `bytes body = 2`, and `optional bool is_final = 3`. The `id` SHALL match the originating `Task.id` as a string. The `body` SHALL be the result payload. The `is_final` field SHALL mark the last result of a task: intermediate streaming results SHALL set it to `false`, and a handler producing a single result SHALL leave it unset (or set it to `true`). Absence of the field SHALL be treated as final (proto3 forbids a `default = true`, so consumers encode `!has_is_final() || is_final()` as "final") so single-shot results remain backward compatible.

#### Scenario: TaskResult carries the originating string task id
- **WHEN** a result for task "happy_fox_runs_k7m2x9p4" with body "hello" is serialized
- **THEN** the serialized bytes SHALL parse back into a `TaskResult` with id="happy_fox_runs_k7m2x9p4" and body "hello"

#### Scenario: Absent is_final is treated as final
- **WHEN** a `TaskResult` is serialized without setting `is_final`
- **THEN** deserializing it SHALL yield a result without `has_is_final()` set, and the consumer-side finality rule (`!has_is_final() || is_final()`) SHALL evaluate to `true`

### Requirement: Task type is an open string
The `type` field SHALL be a string rather than an enum, so new task handler types can be introduced without recompiling the protocol schema.

#### Scenario: Arbitrary type strings are accepted
- **WHEN** a `Task` is created with type="custom-handler"
- **THEN** it SHALL serialize and parse back with type="custom-handler" without enumeration or validation of the type value