# Node Directory

## Purpose

Manages a pool of `Node` instances that connect to nodeagent servers asynchronously, providing runtime membership (add/remove/reconcile), per-node capability and state storage, and candidate selection for pluggable schedulers.

## MODIFIED Requirements

### Requirement: Node implements Completable and CommandHandler
`Node` SHALL implement `event::Completable` and `event::CommandHandler`. It SHALL override `HandleCompletion` for connect completions and `ProcessCommand` for lifecycle commands. A `Node` SHALL own a single `Connection` instance, created after the async connect succeeds. A `Node` SHALL track its status as one of `kInitial`, `kConnecting`, `kConnected`, or `kDisconnected`.

#### Scenario: Node transitions from initial to connecting
- **WHEN** `Node::StartConnect()` is called on a node with status `kInitial`
- **THEN** the node SHALL create a non-blocking socket, submit an async connect via `Dispatcher::PrepareConnect()`, and transition to status `kConnecting`

#### Scenario: Node transitions from connecting to connected on success
- **WHEN** `Node::HandleCompletion(tag=kConnect, res=0)` is called
- **THEN** the node SHALL create a `Connection` with the connected fd, passing the provided `ConnectionFactory`
- **AND** the node SHALL transition to status `kConnected`
- **AND** `Node::GetConnection()` SHALL return a pointer to the newly created `Connection`

#### Scenario: Node transitions from connecting to disconnected on failure
- **WHEN** `Node::HandleCompletion(tag=kConnect, res<0)` is called
- **THEN** the node SHALL close the socket fd
- **AND** the node SHALL transition to status `kDisconnected`
- **AND** `Node::GetConnection()` SHALL return `nullptr`

#### Scenario: Node handles connection closure
- **WHEN** the `Connection` owned by a `Node` sends a `DEFERRED_DELETE` command
- **AND** `Node::ProcessCommand()` receives the command
- **THEN** the node SHALL reset its `connection_` to `nullptr`
- **AND** the node SHALL transition to status `kDisconnected`

#### Scenario: Node reports availability
- **WHEN** `Node::IsAvailable()` is called
- **THEN** it SHALL return `true` if and only if the node's status is `kConnected`
