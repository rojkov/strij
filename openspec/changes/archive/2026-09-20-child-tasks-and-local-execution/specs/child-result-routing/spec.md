# child-result-routing

## ADDED Requirements

### Requirement: Node-local result receiver storage

The nodeagent SHALL provide a `LocalResultReceiverStorage` mapping `task_id → ResultReceiver`, the nodeagent mirror of the gateway's `ResultReceiverStorage`. It SHALL expose `Put(task_id, ReceiverPtr)`, `Get(task_id)`, `Erase(task_id)`, and `Empty()`/`Size()`. The storage is owned and maintained by the bundled `default` local scheduler (`node-local-scheduler`): its `Schedule` SHALL register the child's receiver before submitting the child, and its `HandleFrame` resolves it on inbound outcome frames.

#### Scenario: Child receiver is registered before the child runs

- **WHEN** a workflow handler submits a child with id `C` and receiver `receiver` through the submitter
- **THEN** the bundled `default` scheduler's `Schedule` SHALL `Put("C", receiver)` before the child runs
- **AND** `Get("C")` SHALL return that receiver until it is delivered or erased

#### Scenario: Erase removes the entry

- **WHEN** the final result for `C` has been delivered
- **THEN** `Get("C")` SHALL return null and `Size()` SHALL decrease

### Requirement: Inbound child-outcome frames resolve through the router

`NodeagentTlvHandler` SHALL be a thin seam: it owns core `kNodeAdvertisement` writes and SHALL route every other inbound frame on a node connection to the node-side `NodeagentSchedulerRouter` (the node's frame demux, mirroring the gateway `SchedulerRouter`). The router SHALL dispatch by `type_id` to the scheduler claiming the type in its `HandledFrameTypes()`; the bundled `default` scheduler SHALL be the sole claimant of the child-outcome types `kResult` and `kTaskRejected`, and SHALL deliver them to its `LocalResultReceiverStorage`: `kResult` resolves the receiver by `result.id()`, delivers the body with finality, and erases the entry on the final result; `kTaskRejected` resolves by id and delivers the error. Frames whose id has no registered receiver SHALL be dropped with a warning. Frames with no claiming scheduler SHALL be dropped with a warning by the handler.

#### Scenario: Child result frame delivered to the parent

- **WHEN** an inbound `kResult` frame whose id matches a registered child receiver arrives on any node connection
- **THEN** the receiver SHALL be invoked with the result body and finality
- **AND** a final result SHALL erase the storage entry

#### Scenario: Child rejection frame delivered to the parent

- **WHEN** an inbound `kTaskRejected` frame whose id matches a registered child receiver arrives
- **THEN** the receiver SHALL be invoked with the rejection reason
- **AND** the storage entry SHALL be erased

#### Scenario: Unknown child id is dropped

- **WHEN** an inbound `kResult` or `kTaskRejected` frame carries an id with no registered receiver
- **THEN** the frame SHALL be dropped with a warning and no crash

### Requirement: Receiver-backed child result sender

The nodeagent SHALL provide a `ResultSender` implementation (`StorageResultSender`) bound to the `LocalResultReceiverStorage` and a fixed `task_id`. `Send(TaskResult)` SHALL resolve the receiver by id and deliver the body with finality; a final result SHALL erase the entry. `RegisterOnClose`/`UnregisterOnClose` SHALL be supported (the storage is connection-independent; the hooks may be no-ops or mirror the parent's close contract). This sender is what the bundled `default` scheduler passes to the sender-backed `RunTask` overload for locally-admitted children.

#### Scenario: Local child result resolves through the storage

- **WHEN** a locally-run child's handler calls `Send(TaskResult{id, body, is_final})` on a `StorageResultSender`
- **THEN** the parent's receiver SHALL be delivered `body` with `is_final`
- **AND** on a final result the storage entry SHALL be erased

### Requirement: Gateway node-connection result receiver

The gateway SHALL provide a `ResultReceiver` implementation bound to an `io::Connection`'s outbound mailbox (`NodeConnectionResultReceiver`) — the TLV sibling of `HttpResultReceiver`. `Deliver(body, is_final)` SHALL write a `kResult` frame serialized from a `TaskResult` (`id` → `task_id`, `body`, `is_final`); `DeliverError(reason)` SHALL write a `kTaskRejected` frame. The gateway SHALL store it in `ResultReceiverStorage` keyed by the child's `task_id` and the submitting node's `node_id`, so existing disconnect cleanup applies.

#### Scenario: Result delivered back over the submitting node's connection

- **WHEN** a forwarded child's worker emits a final `kResult` to the gateway and the gateway looks up the child's receiver
- **THEN** the `NodeConnectionResultReceiver` SHALL write a `kResult` frame on the submitting node's connection
- **AND** the gateway SHALL erase the storage entry

#### Scenario: Child error writes a rejection frame

- **WHEN** the gateway decides a forwarded child cannot be scheduled (no matching gateway scheduler, no default)
- **THEN** `DeliverError(reason)` SHALL write a `kTaskRejected` frame on the submitting node's connection
- **AND** the storage entry SHALL be erased

### Requirement: Two-hop child result delivery

The full path for a forwarded child SHALL deliver the child's result to the parent in two hops without new wire types: worker → gateway (`kResult`, node connection), gateway → submitting node (`kResult`, via `NodeConnectionResultReceiver`), submitting node → parent (via `LocalResultReceiverStorage`). Delivery SHALL be correct regardless of which gateway connection carried the original upstream submission.

#### Scenario: Forwarded child completes end to end

- **WHEN** a parent on node A submits a child that runs on node B via gateway G
- **THEN** the child's final result SHALL be delivered to A's parent receiver
- **AND** the storage entry SHALL be erased
- **AND** node B's admission capacity SHALL be released on the final result