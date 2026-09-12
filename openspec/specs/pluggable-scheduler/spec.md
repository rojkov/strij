# pluggable-scheduler

## Purpose

Defines the gateway-side `Scheduler` extension category: how scheduling policies are registered, configured, and selected, how candidates are filtered by `scheduling_protocols`, and the v1 push-based policies.

## Requirements

### Requirement: Scheduler extension interface

The system SHALL define a `Scheduler` interface shared by gateway and nodeagent scheduler extensions with `Schedule(const task::Task&, ResultReceiverPtr)` (fire-and-forget), `RequiredProtocol() -> std::string_view`, `HandleFrame(io::TlvFrame, io::Connection&)` (default no-op), and `HandledFrameTypes() -> std::span<const uint8_t>` (default empty). `TaskOffer` SHALL be removed; the previous `Choose(NodeDirectory&, const TaskOffer&) -> Node*` SHALL be replaced by `Schedule`, and node selection SHALL become a private detail of push-style scheduler implementations. A `GatewaySchedulerFactory` SHALL be registered in `Registry<GatewaySchedulerFactory>` via the existing `REGISTER_FACTORY` macros, following the `ExtensionConfig` configuration pattern, and SHALL be created with a `GatewayFactoryContext` exposing Dispatcher, Logger, NodeDirectory, and ResultReceiverStorage. `Schedule` SHALL be fire-and-forget: the scheduler owns all node I/O for the task's flow, and results or errors SHALL be delivered to the given `ResultReceiver` (routed by `task.id`). For every task handed to `Schedule`, the scheduler SHALL eventually deliver either a result or an error to the receiver.

#### Scenario: Scheduler factory is registered and created

- **WHEN** a `GatewaySchedulerFactory` with name `"capability_aware"` is registered and looked up in `Registry<GatewaySchedulerFactory>`
- **THEN** the factory SHALL be found
- **AND** `Create(config, context)` SHALL return a `std::unique_ptr<Scheduler>`

#### Scenario: Schedule is fire-and-forget

- **WHEN** a scheduler's `Schedule(task, receiver)` is called
- **THEN** it SHALL return without blocking on a node round-trip
- **AND** the receiver SHALL remain valid until the task resolves

#### Scenario: Every scheduled task resolves

- **WHEN** a task is handed to `Schedule` and no node can ever take it
- **THEN** the scheduler SHALL deliver an error to the receiver (the receiver SHALL NOT hang)

### Requirement: Candidate filtering by scheduling_protocols

The gateway SHALL restrict a scheduler's candidates to connected nodes whose advertisement lists the scheduler's `RequiredProtocol()` in `scheduling_protocols`.

#### Scenario: Node without required protocol is excluded

- **WHEN** a scheduler requires protocol `"push"` and a node's `scheduling_protocols` does not contain `"push"`
- **THEN** the node SHALL NOT be selected by the scheduler

### Requirement: round_robin policy

The system SHALL provide a `round_robin` scheduler registered as `"round_robin"` in `Registry<GatewaySchedulerFactory>` that preserves the pre-existing behavior: `Schedule` SHALL select available (connected) nodes in rotation, advancing the selection index on each submission, and submit the task to the chosen node. When no node is available, `Schedule` SHALL deliver an error to the receiver.

#### Scenario: Round-robin rotation over available nodes

- **WHEN** `Schedule` is called repeatedly with two available nodes `A` and `B`
- **THEN** the selected nodes SHALL alternate `A, B, A, B, ...`

#### Scenario: Round-robin errors when none available

- **WHEN** `Schedule` is called with no available nodes
- **THEN** the scheduler SHALL deliver an error to the receiver

### Requirement: capability_aware policy

The system SHALL provide a `capability_aware` scheduler registered as `"capability_aware"` in `Registry<GatewaySchedulerFactory>`. `Schedule` SHALL exclude nodes whose shared free pool capacity is below the task's `ResourceRequirements` or whose per-type concurrency for the task type is exhausted, and SHALL submit the task to the least-loaded node (lowest free-slot ratio, then lowest in-flight count) among the eligible. When no node is eligible, `Schedule` SHALL deliver an error to the receiver.

#### Scenario: Node without required pool capacity is excluded

- **WHEN** a task requires `{"gpu.h100": 1}` and a node's shared free `gpu.h100` is 0
- **THEN** the node SHALL NOT be selected for the task

#### Scenario: Least-loaded node is chosen

- **WHEN** two nodes both satisfy a task's requirements, node `A` with in_flight 2 and node `B` with in_flight 5
- **THEN** the scheduler SHALL select `A`

### Requirement: Scheduler configuration

`GatewayConfig` SHALL expose a list of configured schedulers (see `gateway-config`). The field SHALL be required: when no scheduler is configured, the gateway SHALL fail to start with an error indicating a scheduler must be configured. When configured, the gateway SHALL look up each named factory in `Registry<GatewaySchedulerFactory>` and, if a factory is not found, fail to start with an error naming the missing scheduler. Each configured scheduler SHALL be constructed once at startup.

#### Scenario: Missing scheduler fails startup

- **WHEN** a `GatewayConfig` has no scheduler entries
- **THEN** the gateway SHALL log an error and exit with status 1

#### Scenario: Unknown scheduler name fails startup

- **WHEN** a scheduler entry sets `name = "nonexistent"` and no such factory is registered in `Registry<GatewaySchedulerFactory>`
- **THEN** the gateway SHALL log an error and exit with status 1

### Requirement: GatewayHttpHandler uses the scheduler

The gateway HTTP path SHALL resolve the task's requirements, create a result receiver, register it in `ResultReceiverStorage` keyed by the task ID, and call the scheduler router's `Schedule` (fire-and-forget). The HTTP path SHALL NOT select nodes or write node frames itself; node I/O SHALL belong to the scheduler. When no scheduler matches the task type and no default scheduler exists, the gateway SHALL deliver an error to the receiver (which results in an HTTP error status to the client).

#### Scenario: Task routed through scheduler

- **WHEN** an HTTP task request is received
- **THEN** the gateway SHALL resolve the task's requirements, register a receiver for the task ID, and call `Schedule` on the scheduler router
- **AND** the task submission to the node SHALL be owned by the scheduler

### Requirement: Per-type scheduler routing

The gateway SHALL maintain a scheduler router that dispatches `Schedule(task, receiver)` to the scheduler whose configured task-type match applies; a scheduler entry with no task type SHALL serve as the default for unmatched types. When a task type matches no scheduler and no default is configured, the router SHALL deliver an error to the receiver. The router SHALL implement the `Scheduler` interface itself.

#### Scenario: Type-matched scheduler receives the task

- **WHEN** two schedulers are configured, one matching task type `"infer"` and a default, and a task of type `"infer"` arrives
- **THEN** the `Schedule` call SHALL be dispatched to the `"infer"`-matched scheduler

#### Scenario: Unmatched type falls back to the default scheduler

- **WHEN** a task of type `"render"` arrives and only an `"infer"`-matched scheduler plus a default are configured
- **THEN** the `Schedule` call SHALL be dispatched to the default scheduler

#### Scenario: Unmatched type without a default is rejected

- **WHEN** a task type matches no scheduler and no default scheduler is configured
- **THEN** the router SHALL deliver an error to the receiver

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
