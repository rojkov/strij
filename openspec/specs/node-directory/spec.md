# Node Directory

## Purpose

Manages a pool of `Node` instances that connect to nodeagent servers asynchronously, providing runtime membership (add/remove/reconcile), per-node capability and state storage, and candidate selection for pluggable schedulers.

## Requirements

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
- **WHEN** the `Connection` owned by a `Node` sends a `CLOSE_CONNECTION` command
- **AND** `Node::ProcessCommand()` receives the command
- **THEN** the node SHALL reset its `connection_` to `nullptr`
- **AND** the node SHALL transition to status `kDisconnected`

#### Scenario: Node reports availability
- **WHEN** `Node::IsAvailable()` is called
- **THEN** it SHALL return `true` if and only if the node's status is `kConnected`

### Requirement: NodeDirectory manages Node instances
`NodeDirectory` SHALL accept a `DispatcherSharedPtr` and a `ConnectionFactory`, and SHALL own all `Node` instances via `unique_ptr` keyed by `node_id`. It SHALL provide `AddNode(node_id, address)`, `RemoveNode(node_id)`, and `Reconcile(snapshot)` for runtime membership changes.

#### Scenario: NodeDirectory starts with no nodes
- **WHEN** a `NodeDirectory` is constructed with no node list
- **THEN** it SHALL own zero `Node` instances
- **AND** `GetNodeCount()` SHALL return 0

#### Scenario: NodeDirectory adds a node at runtime
- **WHEN** `AddNode("n1", "host1:9090")` is called
- **THEN** it SHALL own a `Node` for `"n1"` with address `"host1:9090"`
- **AND** the node SHALL begin connecting (status `kConnecting`)

#### Scenario: NodeDirectory removes a node at runtime
- **WHEN** `RemoveNode("n1")` is called on a directory containing `"n1"`
- **THEN** `GetNodeCount()` SHALL decrease by one
- **AND** the node's connection SHALL be closed

### Requirement: NodeDirectory returns next available node
`NodeDirectory` SHALL expose iteration over eligible nodes (connected, and advertising the `scheduling_protocols` requested by a scheduler) for scheduler policies, and SHALL retain a default round-robin selection over available nodes.

#### Scenario: NodeDirectory returns next available node
- **WHEN** `NodeDirectory::GetNextNode()` is called with available nodes
- **THEN** it SHALL return a pointer to the next `Node` (round-robin) where `IsAvailable()` is true
- **AND** it SHALL advance the round-robin index for subsequent calls
- **AND** if no node is available, it SHALL return `nullptr`

#### Scenario: NodeDirectory exposes protocol-filtered candidates
- **WHEN** a scheduler requests candidates with protocol `"push"`
- **THEN** the directory SHALL yield connected nodes whose advertisement lists `"push"` in `scheduling_protocols`

### Requirement: Node owns the socket fd until connect completes
`Node` SHALL own the socket fd returned by `socket()` and transfer ownership to the `Connection` upon successful connect. `Node` SHALL NOT create the `Connection` until the connect succeeds.

#### Scenario: Socket fd lifecycle on success
- **WHEN** `Node::StartConnect()` creates a socket
- **THEN** the socket fd SHALL be stored in `Node::fd_`
- **AND** upon successful connect completion, the fd SHALL be passed to the `Connection` constructor
- **AND** `Node::fd_` SHALL be set to `-1` (ownership transferred)

#### Scenario: Socket fd lifecycle on failure
- **WHEN** `Node::HandleCompletion(kConnect, res<0)` is called
- **THEN** the node SHALL close the fd via `::close(fd_)`
- **AND** `Node::fd_` SHALL be set to `-1`

### Requirement: Node stores advertised capabilities and state
A `Node` SHALL store the `NodeCapabilities` received in its `kNodeAdvertisement` and the latest `NodeState` snapshot, and SHALL expose them via accessors so schedulers can filter and score candidates.

#### Scenario: Node exposes received capabilities
- **WHEN** a `Node` receives a `kNodeAdvertisement` carrying pools, handlers, and `scheduling_protocols`
- **THEN** `Node::GetCapabilities()` SHALL return those values

#### Scenario: Node exposes latest state
- **WHEN** a `Node` receives a `kNodeState` frame
- **THEN** the node's stored state SHALL be updated
- **AND** `Node::GetState()` SHALL return the new values
