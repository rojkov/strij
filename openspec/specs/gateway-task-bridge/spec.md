# Gateway Task Bridge

## Purpose

Bridges HTTP requests at the gateway to nodeagent task processing via TLV connections, managing task lifecycle from submission through result delivery.

## Requirements

### Requirement: ResultReceiverStorage maps task_ids to receivers
`ResultReceiverStorage` SHALL be an associative container mapping `std::string` task_ids to result receiver objects. It SHALL support `Put(task_id, receiver, node_id)`, `Get(task_id)`, and `Erase(task_id)` operations. The `Put` operation SHALL record the association between the task and the node it was routed to. The storage SHALL be thread-safe for concurrent access from HTTP and TLV handlers (if handlers run on different threads).

#### Scenario: Store and retrieve a receiver
- **WHEN** `Put("happy_fox_runs_k7m2x9p4", receiver, "node_A")` is called
- **THEN** `Get("happy_fox_runs_k7m2x9p4")` SHALL return a pointer to the stored receiver
- **AND** `Get("other_id_12345678")` SHALL return nullptr

#### Scenario: Erase a receiver
- **WHEN** `Erase("happy_fox_runs_k7m2x9p4")` is called after `Put("happy_fox_runs_k7m2x9p4", receiver, "node_A")`
- **THEN** `Get("happy_fox_runs_k7m2x9p4")` SHALL return nullptr
- **AND** the task-to-node association SHALL be removed

### Requirement: GatewayHttpHandler creates tasks from HTTP requests
`GatewayHttpHandler` SHALL receive `HttpRequest` messages (path + headers + body), generate a human-readable task ID using `GenerateTaskId()`, extract the task type from the URL path, populate the `Task.parameters` map from request headers matching the `x-strij-` prefix, query `NodeDirectory` for the next available node, create a result receiver, store it in `ResultReceiverStorage` keyed by the generated task ID and the selected node's ID, register a mailbox close callback on the HTTP connection to clean up the receiver if the client drops, build a `Task` protobuf message, serialize it into a TLV frame via `SerializeTlvFrame()`, and submit the task by calling `Connection::Write()` on the selected node's connection.

#### Scenario: HTTP request creates a task
- **WHEN** an HTTP request with a body and an `x-strij-function` header arrives at the gateway
- **THEN** the handler SHALL call `GenerateTaskId()` to obtain a new task ID string
- **AND** extract the task type from the URL path
- **AND** populate `Task.parameters["function"]` from the `x-strij-function` header
- **AND** query `NodeDirectory::GetNextNode()` for the next available node
- **AND** if a node is available, store a result receiver in `ResultReceiverStorage` keyed by the generated task ID string and the selected node's ID
- **AND** register a close callback on the HTTP connection's mailbox that removes the receiver from storage and records completion in the `ExactStateTracker` when the connection drops
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

### Requirement: ResultReceiver delivers results with finality
The `ResultReceiver` interface SHALL provide `Deliver(std::span<const std::byte> value, bool is_final)` where `is_final` marks the last result of a task.

#### Scenario: Intermediate result delivered as non-final
- **WHEN** a non-final result for a task is delivered
- **THEN** `Deliver` SHALL be called with `is_final` set to false

#### Scenario: Last result delivered as final
- **WHEN** the last result of a task is delivered
- **THEN** `Deliver` SHALL be called with `is_final` set to true

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

### Requirement: Gateway pre-establishes TLV connections to nodeagents
Gateway SHALL create a `NodeDirectory` with configured nodeagent addresses on startup. `NodeDirectory::StartConnectAll()` SHALL be called before `Dispatcher::Run()`. The HTTP handler SHALL query `NodeDirectory` for available nodes to submit tasks.

#### Scenario: Gateway connects to multiple nodeagents
- **WHEN** gateway starts with configured nodeagent addresses ["agent1:9090", "agent2:9090"]
- **THEN** gateway SHALL create a `NodeDirectory` with these addresses
- **AND** call `StartConnectAll()` before the event loop starts
- **AND** as nodes connect, they become available for task routing via `GetNextNode()`

#### Scenario: Gateway handles nodeagent connection failure
- **WHEN** a nodeagent fails to connect (async connect returns error)
- **THEN** the corresponding `Node` SHALL transition to `kDisconnected`
- **AND** `NodeDirectory::GetNextNode()` SHALL skip disconnected nodes
- **AND** if no nodes are available, HTTP requests SHALL receive a 503 Service Unavailable response

### Requirement: Simple echo end-to-end flow
A complete echo cycle SHALL work as follows: HTTP client sends request with body → gateway creates a `Task` → TLV task frame with the serialized `Task` is sent to nodeagent → nodeagent parses the `Task` and echoes a `TaskResult` → TLV result frame is returned → gateway parses the `TaskResult` and delivers the result → HTTP client receives response with the original body.

#### Scenario: Echo round-trip
- **WHEN** an HTTP POST with body "hello" is sent to gateway
- **THEN** the gateway SHALL send a TLV TaskSubmission frame carrying a serialized `Task` with a string ID (e.g., "happy_fox_runs_k7m2x9p4"), the type from the URL path, and body "hello" to a nodeagent
- **AND** the nodeagent SHALL parse the `Task` and echo a TLV Result frame carrying a serialized `TaskResult` with the same string ID and body "hello"
- **AND** the gateway SHALL parse the `TaskResult` and deliver "hello" to the HTTP connection as an HTTP response