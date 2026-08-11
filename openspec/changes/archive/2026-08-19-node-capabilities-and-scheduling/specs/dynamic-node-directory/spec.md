# dynamic-node-directory

## Purpose

Defines runtime node membership: a stable node identity, and a `NodeDirectory` that adds, removes, and updates nodes from repeated discovery snapshots instead of being fixed at construction.

## ADDED Requirements

### Requirement: Stable node identity

Each nodeagent SHALL generate a stable `node_id` at startup. The `node_id` SHALL be the membership key for a node in the gateway's directory and SHALL be distinct from the node's `address`. A node re-announced under a different address with the same `node_id` SHALL update the existing record rather than create a new one, SHALL tear down the existing connection (it points at the stale address), and SHALL reconnect to the new address.

#### Scenario: Node reappears under a new address

- **WHEN** a node with `node_id = "n1"` is announced at address `a:9090`
- **AND** later the same `node_id` is announced at address `b:9090`
- **THEN** the gateway SHALL keep one record for `n1`
- **AND** the record SHALL now point at `b:9090`
- **AND** the gateway SHALL close the connection to `a:9090`
- **AND** the gateway SHALL connect to `b:9090`

### Requirement: Runtime add and remove

`NodeDirectory` SHALL provide `AddNode(node_id, address)`, `RemoveNode(node_id)`, and `Reconcile(snapshot)` operations usable after construction. `AddNode` SHALL construct a `Node` and start connecting. `RemoveNode` SHALL disconnect and drop the `Node`.

#### Scenario: Add a node at runtime

- **WHEN** `AddNode("n2", "c:9090")` is called on a directory that does not contain `n2`
- **THEN** the directory SHALL contain a `Node` for `n2`
- **AND** the node SHALL begin connecting

#### Scenario: Remove a node at runtime

- **WHEN** `RemoveNode("n1")` is called on a directory containing `n1`
- **THEN** the directory SHALL no longer contain `n1`
- **AND** the node's connection SHALL be closed

### Requirement: Snapshot reconciliation

`NodeDirectory::Reconcile` SHALL accept a full snapshot of current node identities (from the discovery callback) and diff it against current membership: identities in the snapshot but not present SHALL be added, present but not in the snapshot SHALL be removed, and identities present in both SHALL be kept (updating the address if it changed).

#### Scenario: Reconcile adds and removes

- **WHEN** the directory contains `{A, B}`
- **AND** `Reconcile({A, C})` is called
- **THEN** `A` SHALL be kept
- **AND** `B` SHALL be removed
- **AND** `C` SHALL be added

#### Scenario: Reconcile with an unchanged snapshot is a no-op

- **WHEN** the directory contains `{A, B}`
- **AND** `Reconcile({A, B})` is called
- **THEN** both nodes SHALL be retained with their connections untouched

### Requirement: Discovery snapshots are repeatable

The discovery callback SHALL be invocable multiple times, each time delivering the complete current node set. Each invocation SHALL be reconciled against current membership.

#### Scenario: Multiple discovery snapshots drive membership

- **WHEN** the discovery callback fires with `{A, B}`
- **AND** later fires with `{B, C}`
- **THEN** after the first callback the directory SHALL contain `{A, B}`
- **AND** after the second the directory SHALL contain `{B, C}`

### Requirement: Canonical identity from advertisement

A discovery source that does not know stable node identities (e.g. `static`) SHALL derive `NodeInfo.node_id` from the address. When a `Node`'s advertisement carries a `node_id` that differs from the discovery-provided identity, the directory SHALL rekey the record to the advertised `node_id`.

#### Scenario: Advertisement rekeys a placeholder identity

- **WHEN** a node discovered with derived identity `"host1:9090"` connects and advertises `node_id = "n1"`
- **THEN** the directory SHALL key the node's record by `"n1"`
- **AND** subsequent reconciliation snapshots SHALL refer to the node by `"n1"`
