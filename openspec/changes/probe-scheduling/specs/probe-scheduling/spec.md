## ADDED Requirements

### Requirement: Probe frame exchange

The system SHALL provide a probe scheduling protocol as an extension of the TLV framing. The gateway SHALL send `kTaskProbe` frames (payload `TaskProbe{id, type, requirements}`, never the full task body) to a sampled set of candidate nodes and `kTaskGrant` frames (payload the full serialized `Task`) to the single winning node, and SHALL send `kTaskProbeCancel` frames to relinquish nodes. The node SHALL send `kTaskPull` frames (payload `TaskPull{id}`) to claim a task and `kTaskDecline` frames (payload `TaskDecline{id, reason}`) to refuse a probe. Directions: `kTaskProbe`, `kTaskProbeCancel`, `kTaskGrant` are gateway→node; `kTaskPull`, `kTaskDecline` are node→gateway. A node SHALL only send `kTaskDecline` before it has pulled (pre-commit refusal); after pulling, the only exits are grant (run) or cancel (release).

#### Scenario: Probe carries requirements but not the body

- **WHEN** a gateway sends a `kTaskProbe` for a task with a non-empty body and requirements `{"cpu": 2}`
- **THEN** the frame's `TaskProbe` SHALL carry `id`, `type`, and `requirements` and SHALL NOT contain the task body

#### Scenario: Grant carries the full task

- **WHEN** the gateway grants a task to a node
- **THEN** the `kTaskGrant` frame SHALL carry the full serialized `Task` including its body

#### Scenario: Pull claims a task

- **WHEN** a node has reserved capacity for a task and sends `kTaskPull{id}`
- **THEN** the gateway SHALL treat the first pull for that `id` as the claim

#### Scenario: Decline is a pre-commit refusal

- **WHEN** a node refuses a task (queue full or requirements unsatisfiable) before pulling
- **THEN** the node SHALL send `kTaskDecline{id, reason}` and SHALL NOT pull for that id afterward

#### Scenario: Cancel relinquishes a reservation

- **WHEN** a node receives `kTaskProbeCancel{id}` for a task it holds a preallocation for
- **THEN** the node SHALL release the preallocation and SHALL NOT run the task

### Requirement: Node deferred admission

On `kTaskProbe` receipt, the node SHALL: admit the task immediately if capacity is available and send `kTaskPull`; otherwise enqueue the probe in its bounded queue for later activation; if the queue is full, SHALL send `kTaskDecline{reason:"queue full"}`; if the task's requirements are permanently unsatisfiable (undeclared pool or requirements exceeding a pool total), SHALL send `kTaskDecline{reason:"requirements unsatisfiable"}` instead of enqueueing. The node SHALL walk the queue oldest-first on admission-relevant events (probe arrival, `CAPACITY_RELEASED`), admitting and pulling every probe that now fits, up to a configured concurrency cap. `CAPACITY_RELEASED` arrives via the scheduler's `ProcessCommand` (pure wakeup, `args_ == nullptr`); the node SHALL re-derive capacity from the `AdmissionController` and its own queue.

#### Scenario: Free capacity pulls immediately

- **WHEN** a probe arrives and `Admit` succeeds
- **THEN** the node SHALL send `kTaskPull` and hold the reservation

#### Scenario: Exhausted pool enqueues the probe

- **WHEN** a probe arrives, `Admit` returns ResourceExhausted, and the queue has room
- **THEN** the node SHALL enqueue the probe and SHALL NOT pull yet

#### Scenario: Full queue declines

- **WHEN** a probe arrives and the bounded queue is at capacity
- **THEN** the node SHALL send `kTaskDecline{reason:"queue full"}` and SHALL NOT enqueue

#### Scenario: Unsatisfiable requirements decline without queueing

- **WHEN** a probe's requirements reference an undeclared pool or exceed a pool total
- **THEN** the node SHALL send `kTaskDecline{reason:"requirements unsatisfiable"}` and SHALL NOT enqueue

#### Scenario: Release drains the queue

- **WHEN** a `CAPACITY_RELEASED` command reaches the scheduler and queued probes now fit available capacity
- **THEN** the node SHALL admit and pull the queued probes in FIFO order up to its concurrency cap

### Requirement: Node reservation lifecycle

A pulled probe holds a real admission reservation: preallocated capacity that SHALL be released deterministically. On `kTaskGrant` the node SHALL run the task via the scope-carrying `RunTask` variant (which consumes the held reservation without re-admitting); on `kTaskProbeCancel` or `kTaskDecline` it SHALL release the reservation via `AdmissionController::Release()`. Releasing SHALL trigger the node-global `CAPACITY_RELEASED` broadcast, which re-walks the queue (no explicit re-trigger needed). Cancel/decline handling SHALL be idempotent: a cancel or decline for an unknown or already-released task id is a no-op. A node SHALL NOT run a task it did not pull; a grant for a task with no held reservation SHALL be logged and dropped.

#### Scenario: Grant runs the task with the held reservation

- **WHEN** a pulled probe receives `kTaskGrant{Task}`
- **THEN** the node SHALL run the task via the scope-carrying `RunTask` variant without calling `Admit` again

#### Scenario: Cancel releases the reservation

- **WHEN** a node with a pull-pending reservation receives `kTaskProbeCancel{id}`
- **THEN** the reservation SHALL be released and the task SHALL NOT run

#### Scenario: Decline to a late loser is idempotent

- **WHEN** a node receives a cancel or decline for an id that is no longer held
- **THEN** the node SHALL treat it as a no-op and take no admission or forwarding action

#### Scenario: Stray grant is not executed

- **WHEN** a node receives `kTaskGrant` for a task it has no reservation for (e.g. already released)
- **THEN** the node SHALL log and drop the grant and SHALL NOT execute the task

### Requirement: Gateway candidate sampling

A `"probe"` gateway scheduler SHALL implement `RequiredProtocol() == "probe"` and, on `Schedule(task, receiver)`, select a candidate set from `NodeDirectory::GetCandidates("probe")`: a sample of exactly `min(candidate_count, candidates.size())` nodes, drawn uniformly **without replacement** from a draw deterministic in `task.id`, where `candidate_count` defaults to 2 and is overrideable in config. When no candidate advertises the probe protocol, `Schedule` SHALL deliver an error to the receiver immediately. The scheduler SHALL retain the full task, the receiver, and the probed set per task id.

#### Scenario: No eligible nodes errors immediately

- **WHEN** `Schedule` is called and no connected node advertises `"probe"`
- **THEN** the scheduler SHALL deliver an error to the receiver without waiting for a deadline

#### Scenario: Probe is sent to the sampled candidates

- **WHEN** `Schedule` is called with `candidate_count = k` and at least `k` probe-capable nodes are available
- **THEN** the scheduler SHALL send one `kTaskProbe` to each of `k` distinct candidate nodes, each carrying the task's id, type, and requirements

#### Scenario: Sample clamps to the candidate set

- **WHEN** `candidate_count = 2` but only one probe-capable node is available
- **THEN** the scheduler SHALL probe exactly that one node (k=1) and SHALL NOT error

#### Scenario: Sample is deterministic per task id

- **WHEN** the same task id is scheduled twice against the same candidate set
- **THEN** both draws SHALL select the same candidate subset, and different task ids SHALL be decorrelated across successive probes

### Requirement: Gateway race arbitration

The first `kTaskPull` for a task id SHALL win: the gateway SHALL send `kTaskGrant` (full `Task`) to the winning node, SHALL send `kTaskProbeCancel` to all other probed nodes for that task so they relinquish reservations promptly, and SHALL erase the per-task probe state. A pull for an id that is already granted or unknown SHALL be answered with `kTaskProbeCancel` (the node holds capacity for a task that will not be granted to it). Per node per task, the gateway SHALL send at most one of grant or cancel.

#### Scenario: First pull wins

- **WHEN** two probed nodes pull the same task id and the first pull is processed
- **THEN** the gateway SHALL grant the first node and SHALL cancel the second node

#### Scenario: Losers are canceled at grant time

- **WHEN** a task is granted to one of several probed nodes
- **THEN** the remaining probed nodes SHALL each receive `kTaskProbeCancel` for that id

#### Scenario: Late pull for a granted id is revoked, not duplicated

- **WHEN** a node pulls a task id that was already granted or whose probe state is gone
- **THEN** the gateway SHALL send `kTaskProbeCancel` to that node and SHALL NOT send a second grant

### Requirement: Gateway probe deadline and resolution

Every task handed to a `"probe"` scheduler SHALL resolve: the receiver SHALL NOT hang. A per-task deadline SHALL bound the probing window; on expiry the gateway SHALL send `kTaskProbeCancel` to all still-outstanding probed nodes and deliver an error to the receiver. The deadline SHALL default to 1s and be overrideable in config. A `kTaskDecline` from a probed node SHALL remove that candidate; when all candidates have declined, the gateway SHALL deliver an error to the receiver immediately (no deadline wait). The receiver SHALL remain owned by the scheduler's probe state until grant or terminal error, then SHALL be transferred to `ResultReceiverStorage` keyed by task id with the winning node id, from which results are delivered by the existing result path.

#### Scenario: Deadline expires and errors the receiver

- **WHEN** no node pulls a task before its probe deadline
- **THEN** the gateway SHALL cancel all outstanding probes for the task and deliver an error to the receiver

#### Scenario: All-declined errors immediately

- **WHEN** every probed node declines a task
- **THEN** the gateway SHALL deliver an error to the receiver without waiting for the deadline

#### Scenario: Receiver transfers to storage at grant

- **WHEN** a winner is granted
- **THEN** the receiver SHALL be registered in `ResultReceiverStorage` with the winning node's id and probe state SHALL be erased