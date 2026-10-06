# Spec Delta

## MODIFIED Requirements

### Requirement: GatewayTlvHandler dispatches TLV frames by type_id

`GatewayTlvHandler` SHALL receive `TlvFrame` messages and dispatch based on type_id. For type_id=Result, it SHALL parse the value as a `TaskResult` protobuf message, look up the result receiver by the string task ID from `TaskResult.id`, compute finality with the rule `!has_is_final() || is_final()`, and route the result according to its status: when the status is absent or zero, the handler SHALL call `receiver->Deliver(TaskResult.body, is_final)`; when the status is non-zero, the handler SHALL call `receiver->DeliverError` with the code and `TaskResult.body` as the reason. If the result is final, the handler SHALL remove the receiver from storage; otherwise it SHALL keep the receiver to receive subsequent results of the same task. For type_id=Heartbeat, it SHALL acknowledge the heartbeat (implementation TBD).

#### Scenario: Final result frame delivered to receiver and removed

- **WHEN** a `TlvFrame` with type_id=Result arrives whose `TaskResult.is_final` is true and whose status is absent or zero
- **THEN** the handler SHALL parse the value as a `TaskResult` protobuf message
- **AND** look up the receiver by `TaskResult.id` (a string) in `ResultReceiverStorage`
- **AND** if found, call `receiver->Deliver(TaskResult.body, true)`
- **AND** remove the receiver from storage

#### Scenario: Intermediate result frame keeps the receiver

- **WHEN** a `TlvFrame` with type_id=Result arrives whose `TaskResult.is_final` is false
- **THEN** the handler SHALL call `receiver->Deliver(TaskResult.body, false)`
- **AND** SHALL keep the receiver in storage

#### Scenario: Result frame with unknown task id

- **WHEN** a `TlvFrame` with type_id=Result arrives and its `TaskResult.id` has no matching receiver
- **THEN** the handler SHALL log a warning and discard the frame

#### Scenario: Malformed result frame

- **WHEN** a `TlvFrame` with type_id=Result arrives and its value does not parse as a `TaskResult`
- **THEN** the handler SHALL log a warning and discard the frame

#### Scenario: Failed result is routed as an error

- **WHEN** a `TlvFrame` with type_id=Result arrives whose `TaskResult.status` is non-zero
- **THEN** the handler SHALL call `receiver->DeliverError` with that status and the result body as the reason
- **AND** SHALL NOT call `Deliver` with the body as a result payload

#### Scenario: A failed result still ends the task

- **WHEN** a failed result frame arrives and its result is final
- **THEN** the handler SHALL remove the receiver from storage

#### Scenario: A failed intermediate result does not keep the task alive

- **WHEN** a failed result frame arrives whose result is not final
- **THEN** the handler SHALL NOT keep the receiver for subsequent results, since a task that has failed produces no further results

### Requirement: ResultReceiver delivers results with finality

The `ResultReceiver` interface SHALL provide `Deliver(std::span<const std::byte> value, bool is_final)` where `is_final` marks the last result of a task, and `DeliverError` SHALL take a status code in addition to a human-readable reason, so that a failure is classified by the gateway component that determines it rather than inferred later from the reason text. A component that cannot classify a failure specifically SHALL still pass a non-OK code rather than omitting it.

#### Scenario: Intermediate result delivered as non-final

- **WHEN** a non-final result for a task is delivered
- **THEN** `Deliver` SHALL be called with `is_final` set to false

#### Scenario: Last result delivered as final

- **WHEN** the last result of a task is delivered
- **THEN** `Deliver` SHALL be called with `is_final` set to true

#### Scenario: A failure is classified where it is detected

- **WHEN** a gateway component determines that a task has failed and knows why
- **THEN** it SHALL call `DeliverError` with the code designated for that reason

The components that determine a failure are the gateway scheduler router (no eligible node, no scheduler claims the type), the probe scheduler (deadline exceeded), and the result receiver storage (a node the task was running on disconnected). Each of these knows the cause at the moment it detects it.

#### Scenario: Unclassified failure still passes a code

- **WHEN** a gateway component determines that a task has failed without knowing a specific reason
- **THEN** it SHALL call `DeliverError` with the code for an internal failure rather than omitting the code

#### Scenario: A recovered admission refusal is not reported to the client

- **WHEN** a task is refused admission and a scheduler recovers by routing it elsewhere
- **THEN** no failure SHALL be reported to the client
- **AND** the refusal's reason SHALL remain available for logging at the point of the decision

#### Scenario: An unrecovered admission refusal is reported as a failure

- **WHEN** a task is refused admission and no scheduler recovers it
- **THEN** the client SHALL receive a failure whose code reflects the refusal rather than a success
