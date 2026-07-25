# Gateway Task Bridge

## Purpose

Bridges HTTP requests at the gateway to nodeagent task processing via TLV connections, managing task lifecycle from submission through result delivery.

## Requirements

### Requirement: ResultReceiverStorage maps task_ids to receivers
`ResultReceiverStorage` SHALL be an associative container mapping `uint64_t` task_ids to result receiver objects. It SHALL support `put(task_id, receiver)`, `get(task_id)`, and `erase(task_id)` operations. The storage SHALL be thread-safe for concurrent access from HTTP and TLV handlers (if handlers run on different threads).

#### Scenario: Store and retrieve a receiver
- **WHEN** `put(42, receiver)` is called
- **THEN** `get(42)` SHALL return a pointer to the stored receiver
- **AND** `get(99)` SHALL return nullptr

#### Scenario: Erase a receiver
- **WHEN** `erase(42)` is called after `put(42, receiver)`
- **THEN** `get(42)` SHALL return nullptr

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

### Requirement: GatewayTlvHandler dispatches TLV frames by type_id
`GatewayTlvHandler` SHALL receive `TlvFrame` messages and dispatch based on type_id. For type_id=Result, it SHALL look up the result receiver by task_id and deliver the result. For type_id=Heartbeat, it SHALL acknowledge the heartbeat (implementation TBD).

#### Scenario: Result frame delivered to receiver
- **WHEN** a `TlvFrame` with type_id=Result arrives
- **THEN** the handler SHALL extract task_id from the first 8 bytes of value
- **AND** look up the receiver by task_id in `ResultReceiverStorage`
- **AND** if found, call `receiver->Deliver(value)` with the remaining payload (after task_id)
- **AND** remove the receiver from storage (simple echo = single result)

#### Scenario: Result frame with unknown task_id
- **WHEN** a `TlvFrame` with type_id=Result arrives and task_id has no matching receiver
- **THEN** the handler SHALL log a warning and discard the frame

### Requirement: NodeagentTlvHandler echoes task bodies
`NodeagentTlvHandler` SHALL receive `TlvFrame` messages, and for type_id=TaskSubmission, extract task_id and payload from the value, and echo back a `TlvFrame` with type_id=Result and the same task_id + payload in the value.

#### Scenario: Nodeagent echoes a task
- **WHEN** a `TlvFrame` with type_id=TaskSubmission arrives at the nodeagent
- **THEN** the handler SHALL extract task_id and payload from the value
- **AND** send a `TlvFrame` with type_id=Result and value containing [task_id][payload] back on the same connection

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
A complete echo cycle: HTTP client sends request with body → gateway creates task → TLV task frame sent to nodeagent → nodeagent echoes payload → TLV result frame returned → gateway delivers result → HTTP client receives response with original body.

#### Scenario: Echo round-trip
- **WHEN** an HTTP POST with body "hello" is sent to gateway
- **THEN** the gateway SHALL send a TLV TaskSubmission frame with the body "hello" to a nodeagent
- **AND** the nodeagent SHALL echo "hello" back as a TLV Result frame
- **AND** the gateway SHALL deliver "hello" to the HTTP connection as an HTTP response
