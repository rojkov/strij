## MODIFIED Requirements

### Requirement: GatewayHttpHandler creates tasks from HTTP requests
`GatewayHttpHandler` SHALL receive `HttpRequest` messages (path + headers + body), generate a human-readable task ID using `GenerateTaskId()`, extract the task type from the URL path, populate the `Task.parameters` map from request headers matching the `x-strij-` prefix, resolve the task's hardware requirements from the populated parameters via the configured `RequirementsResolver`, query the scheduler for a node, create a result receiver, store it in `ResultReceiverStorage` keyed by the generated task ID and the selected node's ID, register a mailbox close callback on the HTTP connection to clean up the receiver if the client drops, build a `Task` protobuf message carrying the resolved requirements, serialize it into a TLV frame via `SerializeTlvFrame()`, and submit the task by calling `Connection::Write()` on the selected node's connection.

#### Scenario: HTTP request creates a task
- **WHEN** an HTTP request with a body and an `x-strij-function` header arrives at the gateway
- **THEN** the handler SHALL call `GenerateTaskId()` to obtain a new task ID string
- **AND** extract the task type from the URL path
- **AND** populate `Task.parameters["function"]` from the `x-strij-function` header
- **AND** resolve hardware requirements from the populated parameters
- **AND** if a node is available, store a result receiver in `ResultReceiverStorage` keyed by the generated task ID string and the selected node's ID
- **AND** register a close callback on the HTTP connection's mailbox that removes the receiver from storage and records completion in the `ExactStateTracker` when the connection drops
- **AND** build a `Task` with the generated string ID, the extracted type, the request body, and the populated parameters
- **AND** set the `Task.requirements` field to the resolved requirements before serializing
- **AND** serialize the `Task` as protobuf and wrap it in a TLV frame with type_id=TaskSubmission via `SerializeTlvFrame()`
- **AND** call `Connection::Write(frame)` on the selected node's connection
- **AND** if no node is available, respond with 503 Service Unavailable

#### Scenario: Resource headers become carried requirements
- **WHEN** an HTTP request arrives with headers `x-strij-resources-cpu: 2` and `x-strij-resources-gpu.h100: 1`
- **THEN** the submitted `Task` SHALL carry `requirements.resources = {"cpu": 2, "gpu.h100": 1}`

#### Scenario: No resource headers yields empty requirements on the wire
- **WHEN** an HTTP request arrives with no resource-declaring headers
- **THEN** the submitted `Task` SHALL carry empty `requirements`
- **AND** the requirements SHALL be resolved before node selection so routing and admission use identical values

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
