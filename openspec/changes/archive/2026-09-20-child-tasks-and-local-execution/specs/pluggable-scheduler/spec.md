# pluggable-scheduler

## ADDED Requirements

### Requirement: Gateway accepts upstream child submissions

The gateway's `SchedulerRouter` SHALL claim the `kTaskSubmission` TLV frame type on connections owned by gateway `Node`s. On such a frame it SHALL parse the `task::Task`, construct a `NodeConnectionResultReceiver` bound to the submitting connection, register the receiver in `ResultReceiverStorage` keyed by the task id and the submitting node's id, and dispatch the task through its normal `Schedule` routing (type match, default fallback, or error when unconsumed). The router's `HandledFrameTypes()` SHALL therefore include `kTaskSubmission` alongside any protocol-owned types. Existing built-in gateway frame handling (`kNodeAdvertisement`, `kNodeState`, `kTaskRejected`, `kResult`, `kHeartbeat`) SHALL be unchanged.

#### Scenario: Upstream child submission routes through the router

- **WHEN** a node connection carries an inbound `kTaskSubmission` frame with a serialized `Task`
- **THEN** the router SHALL parse the task and dispatch `Schedule` to the scheduler matching the task type (or the default)
- **AND** a `NodeConnectionResultReceiver` SHALL be stored for the task id bound to the submitting connection

#### Scenario: Unmatched upstream child is rejected back to the node

- **WHEN** an upstream child's type matches no gateway scheduler and no default is configured
- **THEN** the router SHALL `DeliverError` through the node-connection receiver
- **AND** a `kTaskRejected` frame SHALL be written on the submitting connection

#### Scenario: Node disconnect cleans up pending upstream children

- **WHEN** the submitting node disconnects with outstanding upstream children
- **THEN** the node-connection receivers SHALL be cleaned up by the existing `ResultReceiverStorage::NotifyNodeDisconnected` path for that node id

### Requirement: Node-connection receiver mirrors HTTP receiver

The gateway SHALL treat a `NodeConnectionResultReceiver` exactly like an `HttpResultReceiver` for lifecycle purposes: either receiver SHALL be stored in `ResultReceiverStorage`, looked up by `task_id`, delivered on `kResult` frames, and erased on the final result or on disconnect, with the state tracker recording completions identically. The only difference SHALL be the write seam (TLV frames over a node connection vs HTTP over a client connection).

#### Scenario: Forwarded child completes and releases the storage entry

- **WHEN** a worker emits a final `kResult` for an upstream child on a gateway connection
- **THEN** the gateway SHALL look up the child's node-connection receiver, deliver the body, erase the entry, and record the completion
- **AND** the node-connection receiver SHALL write the corresponding `kResult` frame back to the submitting node