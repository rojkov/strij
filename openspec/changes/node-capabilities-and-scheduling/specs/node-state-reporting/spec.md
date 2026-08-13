# node-state-reporting

## Purpose

Defines how nodeagents report current state (pool usage, per-type in-flight counts) to gateways over the heartbeat channel, how gateways track that state with exact accounting, and how nodeagents enforce capacity through admission and rejection.

## ADDED Requirements

### Requirement: Nodeagent state accounting

The nodeagent SHALL maintain per-pool in-use counters and per-task-type in-flight counts derived from admissions, completions, and rejections. The nodeagent SHALL reserve pool and concurrency capacity when it admits a task and SHALL release it when the task completes.

#### Scenario: Admission reserves capacity

- **WHEN** a task requiring `{"cpu": 2}` is admitted on a node with pool `cpu` total 16 and in_use 0
- **THEN** the node's `cpu` in_use SHALL become 2

#### Scenario: Completion releases capacity

- **WHEN** the task from the previous scenario completes
- **THEN** the node's `cpu` in_use SHALL return to 0

### Requirement: NodeState snapshot frame

The system SHALL define a `NodeState` protobuf message (package `strij.node`) with `node_id`, `seq` (monotonic), `timestamp`, `repeated PoolUsage pools` (each `pool` + `in_use`), `in_flight` (node-wide), and `repeated TypeUsage type_usage` (each `task_type` + `in_flight`). The nodeagent SHALL send it as a `kNodeState` frame on every established gateway connection at `heartbeat_interval` (Duration, default 10s).

#### Scenario: NodeState is sent periodically

- **WHEN** a nodeagent has an established connection and `heartbeat_interval = 1s`
- **THEN** the nodeagent SHALL send a `kNodeState` frame on that connection at least once per second

#### Scenario: NodeState carries pool usage

- **WHEN** a `NodeState` reports pool `gpu.h100` with in_use 1 and in_flight 3
- **THEN** the gateway SHALL read those values from the parsed message

### Requirement: Gateway exact state tracking

The system SHALL provide an `ExactStateTracker` that maintains per-node state on the gateway by counting task submissions sent (increment), final results and rejections received (decrement), and applying `kNodeState` snapshots as a verification/correction signal.

#### Scenario: Tracker increments on send

- **WHEN** the gateway sends a task to a node
- **THEN** the tracker SHALL increment the node's in-flight and reserved-pool counts

#### Scenario: Tracker decrements on final result

- **WHEN** the gateway receives a final result for a task previously sent to a node
- **THEN** the tracker SHALL decrement the node's in-flight and reserved-pool counts

### Requirement: Nodeagent admission control

On each `kTaskSubmission`, the nodeagent SHALL admit the task only if the shared free capacity of every requested pool is at least the task's requirement and the task type has concurrency headroom (unlimited when the handler's `concurrency` is `0` or omitted). On admission it SHALL reserve capacity; otherwise it SHALL NOT reserve and SHALL send a `kTaskRejected` frame.

#### Scenario: Task admitted within capacity

- **WHEN** a task requiring `{"gpu.h100": 1}` is submitted to a node whose shared free `gpu.h100` is 1 and whose handler concurrency has headroom
- **THEN** the task SHALL be admitted and dispatched to the task handler

#### Scenario: Task rejected when a pool is exhausted

- **WHEN** a task requiring `{"gpu.h100": 1}` is submitted to a node whose shared free `gpu.h100` is 0
- **THEN** the nodeagent SHALL send `kTaskRejected` for the task id
- **AND** the task SHALL NOT be dispatched to the task handler

### Requirement: TaskRejected message and delivery

The system SHALL define a `TaskRejected` protobuf message with `id` (matching the originating `Task.id`) and `reason`. The gateway SHALL route a received `kTaskRejected` by task id to the corresponding `ResultReceiver` and deliver an error outcome to the HTTP client.

#### Scenario: Rejection reaches the HTTP client

- **WHEN** the gateway receives `kTaskRejected` for task id `t1`
- **THEN** the `ResultReceiver` registered for `t1` SHALL deliver an error outcome
- **AND** the task SHALL NOT hang the client connection

### Requirement: No retry on rejection in v1

The gateway SHALL NOT automatically retry a rejected task on another node in v1.

#### Scenario: Rejected task is not retried

- **WHEN** a `kTaskRejected` is received for a task
- **THEN** the gateway SHALL NOT resend the task to another node
