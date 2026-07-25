# Node Directory

## Purpose

Manages a pool of `Node` instances that connect to nodeagent servers asynchronously, providing round-robin selection of available nodes for task routing.

## Requirements

### Requirement: Node is an IOObject that owns a Connection
`Node` SHALL inherit from `event::IOObject` and implement `HandleCompletion` and `ProcessCommand`. A `Node` SHALL own a single `Connection` instance, created after the async connect succeeds. A `Node` SHALL track its status as one of `kInitial`, `kConnecting`, `kConnected`, or `kDisconnected`.

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
`NodeDirectory` SHALL accept a `DispatcherSharedPtr`, a list of node address strings in `"host:port"` format, and a `ConnectionFactory`. It SHALL create one `Node` per address and own all nodes via `unique_ptr`.

#### Scenario: NodeDirectory is constructed with addresses
- **WHEN** a `NodeDirectory` is created with addresses `["host1:9090", "host2:9090"]`
- **THEN** it SHALL create two `Node` instances, each with the corresponding address
- **AND** both nodes SHALL have status `kInitial`

#### Scenario: NodeDirectory starts connecting all nodes
- **WHEN** `NodeDirectory::StartConnectAll()` is called
- **THEN** it SHALL call `Node::StartConnect()` on every node

#### Scenario: NodeDirectory returns next available node
- **WHEN** `NodeDirectory::GetNextNode()` is called
- **THEN** it SHALL return a pointer to the next `Node` (round-robin) where `IsAvailable()` is true
- **AND** it SHALL advance the round-robin index for subsequent calls
- **AND** if no node is available, it SHALL return `nullptr`

#### Scenario: NodeDirectory reports node counts
- **WHEN** `NodeDirectory::GetNodeCount()` is called
- **THEN** it SHALL return the total number of managed nodes
- **AND** `NodeDirectory::GetAvailableCount()` SHALL return the number of nodes where `IsAvailable()` is true

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
