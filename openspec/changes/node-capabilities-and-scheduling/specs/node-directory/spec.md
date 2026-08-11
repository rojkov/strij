# Node Directory

## Purpose

Manages a pool of `Node` instances that connect to nodeagent servers asynchronously, providing runtime membership (add/remove/reconcile), per-node capability and state storage, and candidate selection for pluggable schedulers.

## MODIFIED Requirements

### Requirement: NodeDirectory manages Node instances
`NodeDirectory` SHALL accept a `DispatcherSharedPtr` and a `ConnectionFactory`, and SHALL own all `Node` instances via `unique_ptr` keyed by `node_id`. It SHALL provide `AddNode(node_id, address)`, `RemoveNode(node_id)`, and `Reconcile(snapshot)` for runtime membership changes.

#### Scenario: NodeDirectory starts with no nodes
- **WHEN** a `NodeDirectory` is constructed with no node list
- **THEN** it SHALL own zero `Node` instances
- **AND** `GetNodeCount()` SHALL return 0

#### Scenario: NodeDirectory adds a node at runtime
- **WHEN** `AddNode("n1", "host1:9090")` is called
- **THEN** it SHALL own a `Node` for `"n1"` with address `"host1:9090"`
- **AND** the node SHALL have status `kInitial`

#### Scenario: NodeDirectory removes a node at runtime
- **WHEN** `RemoveNode("n1")` is called on a directory containing `"n1"`
- **THEN** `GetNodeCount()` SHALL decrease by one
- **AND** the node's connection SHALL be closed

#### Scenario: NodeDirectory starts connecting all nodes
- **WHEN** `NodeDirectory::StartConnectAll()` is called
- **THEN** it SHALL call `Node::StartConnect()` on every node

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

## ADDED Requirements

### Requirement: Node stores advertised capabilities and state
A `Node` SHALL store the `NodeCapabilities` received in its `kNodeAdvertisement` and the latest `NodeState` snapshot, and SHALL expose them via accessors so schedulers can filter and score candidates.

#### Scenario: Node exposes received capabilities
- **WHEN** a `Node` receives a `kNodeAdvertisement` carrying pools, handlers, and `scheduling_protocols`
- **THEN** `Node::GetCapabilities()` SHALL return those values

#### Scenario: Node exposes latest state
- **WHEN** a `Node` receives a `kNodeState` frame
- **THEN** the node's stored state SHALL be updated
- **AND** `Node::GetState()` SHALL return the new values
