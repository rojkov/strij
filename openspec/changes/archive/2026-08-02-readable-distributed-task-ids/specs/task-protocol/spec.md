## MODIFIED Requirements

### Requirement: Task message schema
The system SHALL define a Protobuf message `Task` in package `strij.task` with fields `string id = 1`, `string type = 2`, and `bytes body = 3`. The `id` SHALL be a human-readable, randomly generated string identifier (e.g., `happy_fox_runs_k7m2x9p4`) used to route results back to the originating HTTP client. The `type` SHALL identify the task handler intended to process the task. The `body` SHALL be the task payload.

#### Scenario: Task carries string id, type, and body
- **WHEN** a task with id="happy_fox_runs_k7m2x9p4", type="echo", and body "hello" is serialized
- **THEN** the serialized bytes SHALL parse back into a `Task` with id="happy_fox_runs_k7m2x9p4", type="echo", and body "hello"

### Requirement: TaskResult message schema
The system SHALL define a Protobuf message `TaskResult` in package `strij.task` with fields `string id = 1` and `bytes body = 2`. The `id` SHALL match the originating `Task.id` as a string. The `body` SHALL be the result payload.

#### Scenario: TaskResult carries the originating string task id
- **WHEN** a result for task "happy_fox_runs_k7m2x9p4" with body "hello" is serialized
- **THEN** the serialized bytes SHALL parse back into a `TaskResult` with id="happy_fox_runs_k7m2x9p4" and body "hello"

### Requirement: Task type is an open string
The `type` field SHALL be a string rather than an enum, so new task handler types can be introduced without recompiling the protocol schema.

#### Scenario: Arbitrary type strings are accepted
- **WHEN** a `Task` is created with type="custom-handler"
- **THEN** it SHALL serialize and parse back with type="custom-handler" without enumeration or validation of the type value
