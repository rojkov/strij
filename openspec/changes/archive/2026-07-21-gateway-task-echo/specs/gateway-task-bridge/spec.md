## ADDED Requirements

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
`GatewayHttpHandler` SHALL receive `HttpRequest` messages (body + metadata), generate a monotonic task_id, select a nodeagent connection via round-robin, create a result receiver, store it in `ResultReceiverStorage`, and submit the task as a TLV frame to the selected nodeagent.

#### Scenario: HTTP request creates a task
- **WHEN** an HTTP request with a body arrives at the gateway
- **THEN** the handler SHALL generate a new task_id
- **AND** select the next nodeagent connection (round-robin)
- **AND** store a result receiver in `ResultReceiverStorage` keyed by task_id
- **AND** send a TLV frame with type_id=TaskSubmission and value containing [task_id][HTTP body] to the selected nodeagent

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
Gateway SHALL connect to configured nodeagent addresses on startup and hold `TlvSender` objects for each connection. The HTTP handler SHALL use these senders to submit tasks.

#### Scenario: Gateway connects to multiple nodeagents
- **WHEN** gateway starts with configured nodeagent addresses [agent1:9090, agent2:9090]
- **THEN** gateway SHALL establish TCP connections to both nodeagents
- **AND** create a `TlvSender` for each connection
- **AND** the HTTP handler SHALL distribute tasks across senders via round-robin

#### Scenario: Gateway handles nodeagent connection failure
- **WHEN** a connection to a nodeagent fails on startup
- **THEN** gateway SHALL log an error and continue with remaining connections
- **AND** if no connections are available, HTTP requests SHALL receive a 503 Service Unavailable response

### Requirement: Simple echo end-to-end flow
A complete echo cycle: HTTP client sends request with body → gateway creates task → TLV task frame sent to nodeagent → nodeagent echoes payload → TLV result frame returned → gateway delivers result → HTTP client receives response with original body.

#### Scenario: Echo round-trip
- **WHEN** an HTTP POST with body "hello" is sent to gateway
- **THEN** the gateway SHALL send a TLV TaskSubmission frame with the body "hello" to a nodeagent
- **AND** the nodeagent SHALL echo "hello" back as a TLV Result frame
- **AND** the gateway SHALL deliver "hello" to the HTTP connection as an HTTP response
