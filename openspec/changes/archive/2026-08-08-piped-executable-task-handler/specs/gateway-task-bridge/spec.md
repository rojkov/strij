## MODIFIED Requirements

### Requirement: GatewayHttpHandler creates tasks from HTTP requests
`GatewayHttpHandler` SHALL receive `HttpRequest` messages (path + headers + body), generate a human-readable task ID using `GenerateTaskId()`, extract the task type from the URL path, populate the `Task.parameters` map from request headers matching the `x-strij-` prefix, query `NodeDirectory` for the next available node, create a result receiver, store it in `ResultReceiverStorage`, build a `Task` protobuf message, serialize it into a TLV frame via `SerializeTlvFrame()`, and submit the task by calling `Connection::Write()` on the selected node's connection.

#### Scenario: HTTP request creates a task
- **WHEN** an HTTP request with a body and an `x-strij-function` header arrives at the gateway
- **THEN** the handler SHALL call `GenerateTaskId()` to obtain a new task ID string
- **AND** extract the task type from the URL path
- **AND** populate `Task.parameters["function"]` from the `x-strij-function` header
- **AND** query `NodeDirectory::GetNextNode()` for the next available node
- **AND** if a node is available, store a result receiver in `ResultReceiverStorage` keyed by the generated task ID string
- **AND** build a `Task` with the generated string ID, the extracted type, the request body, and the populated parameters
- **AND** serialize the `Task` as protobuf and wrap it in a TLV frame with type_id=TaskSubmission via `SerializeTlvFrame()`
- **AND** call `Connection::Write(frame)` on the selected node's connection
- **AND** if no node is available, respond with 503 Service Unavailable

#### Scenario: Path without /tasks/ prefix is rejected
- **WHEN** an HTTP request path does not start with `/tasks/`
- **THEN** the handler SHALL respond with 404 Not Found
- **AND** SHALL NOT submit a task

#### Scenario: Empty task type is rejected
- **WHEN** the extracted task type is empty (e.g. path is `/tasks/`)
- **THEN** the handler SHALL respond with 400 Bad Request
- **AND** SHALL NOT submit a task

#### Scenario: Query string is stripped from the type
- **WHEN** the request path is `/tasks/echo?param=1`
- **THEN** the handler SHALL extract the task type "echo", ignoring the query string

### Requirement: GatewayTlvHandler dispatches TLV frames by type_id
`GatewayTlvHandler` SHALL receive `TlvFrame` messages and dispatch based on type_id. For type_id=Result, it SHALL parse the value as a `TaskResult` protobuf message, look up the result receiver by the string task ID from `TaskResult.id`, compute finality with the rule `!has_is_final() || is_final()`, and call `receiver->Deliver(TaskResult.body, is_final)`. If the result is final, the handler SHALL remove the receiver from storage; otherwise it SHALL keep the receiver to receive subsequent results of the same task. For type_id=Heartbeat, it SHALL acknowledge the heartbeat (implementation TBD).

#### Scenario: Final result frame delivered to receiver and removed
- **WHEN** a `TlvFrame` with type_id=Result arrives whose `TaskResult.is_final` is true
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

## ADDED Requirements

### Requirement: ResultReceiver delivers results with finality
The `ResultReceiver` interface SHALL provide `Deliver(std::span<const std::byte> value, bool is_final)` where `is_final` marks the last result of a task.

#### Scenario: Intermediate result delivered as non-final
- **WHEN** a non-final result for a task is delivered
- **THEN** `Deliver` SHALL be called with `is_final` set to false

#### Scenario: Last result delivered as final
- **WHEN** the last result of a task is delivered
- **THEN** `Deliver` SHALL be called with `is_final` set to true
