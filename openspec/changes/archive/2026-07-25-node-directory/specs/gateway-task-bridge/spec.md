## MODIFIED Requirements

### Requirement: GatewayHttpHandler creates tasks from HTTP requests
`GatewayHttpHandler` SHALL receive `HttpRequest` messages (body + metadata), generate a monotonic task_id, query `NodeDirectory` for the next available node, create a result receiver, store it in `ResultReceiverStorage`, serialize a TLV frame via `SerializeTlvFrame()`, and submit the task by calling `Connection::Write()` on the selected node's connection.

#### Scenario: HTTP request creates a task
- **WHEN** an HTTP request with a body arrives at the gateway
- **THEN** the handler SHALL generate a new task_id
- **AND** query `NodeDirectory::GetNextNode()` for the next available node
- **AND** if a node is available, store a result receiver in `ResultReceiverStorage` keyed by task_id
- **AND** serialize a TLV frame with type_id=TaskSubmission and value containing [task_id][HTTP body] via `SerializeTlvFrame()`
- **AND** call `Connection::Write(frame)` on the selected node's connection
- **AND** if no node is available, respond with 503 Service Unavailable

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
