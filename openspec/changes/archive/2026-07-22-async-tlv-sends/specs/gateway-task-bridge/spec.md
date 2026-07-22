## MODIFIED Requirements

### Requirement: GatewayHttpHandler creates tasks from HTTP requests
`GatewayHttpHandler` SHALL receive `HttpRequest` messages (body + metadata), generate a monotonic task_id, select a nodeagent connection via round-robin, create a result receiver, store it in `ResultReceiverStorage`, serialize a TLV frame via `SerializeTlvFrame()`, and submit the task by calling `Connection::Write()` on the selected nodeagent connection.

#### Scenario: HTTP request creates a task
- **WHEN** an HTTP request with a body arrives at the gateway
- **THEN** the handler SHALL generate a new task_id
- **AND** select the next nodeagent connection (round-robin)
- **AND** store a result receiver in `ResultReceiverStorage` keyed by task_id
- **AND** serialize a TLV frame with type_id=TaskSubmission and value containing [task_id][HTTP body] via `SerializeTlvFrame()`
- **AND** call `Connection::Write(frame)` on the selected nodeagent connection

### Requirement: Gateway pre-establishes TLV connections to nodeagents
Gateway SHALL connect to configured nodeagent addresses on startup and hold `Connection*` references for each connection. The HTTP handler SHALL use these connections to submit tasks via `Connection::Write()`.

#### Scenario: Gateway connects to multiple nodeagents
- **WHEN** gateway starts with configured nodeagent addresses [agent1:9090, agent2:9090]
- **THEN** gateway SHALL establish TCP connections to both nodeagents
- **AND** store `Connection*` references for each connection
- **AND** the HTTP handler SHALL distribute tasks across connections via round-robin

#### Scenario: Gateway handles nodeagent connection failure
- **WHEN** a connection to a nodeagent fails on startup
- **THEN** gateway SHALL log an error and continue with remaining connections
- **AND** if no connections are available, HTTP requests SHALL receive a 503 Service Unavailable response
