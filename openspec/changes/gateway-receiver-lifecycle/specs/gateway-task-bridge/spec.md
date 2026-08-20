# gateway-task-bridge (delta)

## MODIFIED Requirements

### Requirement: ResultReceiverStorage maps task_ids to receivers
`ResultReceiverStorage` SHALL be an associative container mapping `std::string` task_ids to result receiver objects. It SHALL support `put(task_id, receiver, node_id)`, `get(task_id)`, and `erase(task_id)` operations. The `put` operation SHALL record the association between the task and the node it was routed to. The storage SHALL be thread-safe for concurrent access from HTTP and TLV handlers (if handlers run on different threads).

#### Scenario: Store and retrieve a receiver
- **WHEN** `put("happy_fox_runs_k7m2x9p4", receiver, "node_A")` is called
- **THEN** `get("happy_fox_runs_k7m2x9p4")` SHALL return a pointer to the stored receiver
- **AND** `get("other_id_12345678")` SHALL return nullptr

#### Scenario: Erase a receiver
- **WHEN** `erase("happy_fox_runs_k7m2x9p4")` is called after `put("happy_fox_runs_k7m2x9p4", receiver, "node_A")`
- **THEN** `get("happy_fox_runs_k7m2x9p4")` SHALL return nullptr
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
