# Spec Delta

## MODIFIED Requirements

### Requirement: TaskResult message schema

The system SHALL define a Protobuf message `TaskResult` in package `strij.task` with fields `string id = 1`, `bytes body = 2`, and `optional bool is_final = 3`, plus a `TaskStatus` enum in the same package carried as `optional TaskStatus status = 4`. The `id` SHALL match the originating `Task.id` as a string. The `body` SHALL be the result payload. The `is_final` field SHALL mark the last result of a task: intermediate streaming results SHALL set it to `false`, and a handler producing a single result SHALL leave it unset (or set it to `true`). Absence of the field SHALL be treated as final (proto3 forbids a `default = true`, so consumers encode `!has_is_final() || is_final()` as "final").

The `status` field SHALL classify a failure: absence, or the `TaskStatus` member whose value is zero, SHALL mean success, and any other value SHALL mean failure, in which case `body` carries an error message. The field SHALL be optional so that results produced before it existed remain readable as successes.

#### Scenario: TaskResult carries the originating string task id

- **WHEN** a result for task "happy_fox_runs_k7m2x9p4" with body "hello" is serialized
- **THEN** the serialized bytes SHALL parse back into a `TaskResult` with id="happy_fox_runs_k7m2x9p4" and body "hello"

#### Scenario: Absent is_final is treated as final

- **WHEN** a `TaskResult` is serialized without setting `is_final`
- **THEN** deserializing it SHALL yield a result without `has_is_final()` set, and the consumer-side finality rule (`!has_is_final() || is_final()`) SHALL evaluate to `true`

#### Scenario: Absent status is treated as success

- **WHEN** a `TaskResult` is serialized without setting `status`
- **THEN** deserializing it SHALL yield a result without `has_status()` set, and the consumer SHALL read that result as a success

#### Scenario: A failure carries a status and an error body

- **WHEN** a `TaskResult` is serialized with a non-OK `status` and a body describing the failure
- **THEN** the consumer SHALL read the result as a failure and SHALL interpret the body as the error message

#### Scenario: An unrecognized status value survives the round trip as a failure

- **WHEN** a `TaskResult` is serialized with a `status` value that has no `TaskStatus` member in the consumer's schema, because the producer is newer
- **THEN** deserializing it SHALL yield that numeric value rather than an error, and the consumer SHALL read the result as a failure
