# task-protocol

## Purpose

Defines the Protobuf message schemas used to carry tasks and results between gateway and nodeagent over TLV frames, including the task rejection message.

## ADDED Requirements

### Requirement: TaskRejected message schema
The system SHALL define a Protobuf message `TaskRejected` in package `strij.task` with fields `string id = 1` and `string reason = 2`. The `id` SHALL match the originating `Task.id`; the `reason` SHALL describe why the task was not admitted (e.g. a pool was exhausted or concurrency was at capacity).

#### Scenario: TaskRejected carries the originating task id
- **WHEN** a `TaskRejected` for task "happy_fox_runs_k7m2x9p4" with reason "gpu.h100 exhausted" is serialized
- **THEN** the serialized bytes SHALL parse back into a `TaskRejected` with id="happy_fox_runs_k7m2x9p4" and reason="gpu.h100 exhausted"

#### Scenario: TaskRejected reason is open
- **WHEN** a `TaskRejected` is created with an arbitrary reason string
- **THEN** it SHALL serialize and parse back without enumeration or validation of the reason value
