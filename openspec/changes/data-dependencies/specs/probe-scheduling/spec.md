# Probe Scheduling — delta spec

This is a **delta** spec for `openspec/specs/probe-scheduling/spec.md`. It describes modifications introduced by the `data-dependencies` change (Phase 3).

## MODIFIED Requirements

### Requirement: Probe frame exchange

The system SHALL provide a probe scheduling protocol as an extension of the TLV framing. The gateway SHALL send `kTaskProbe` frames (payload `TaskProbe{id, type, requirements, deps}`, never the full task body) to a sampled set of candidate nodes and `kTaskGrant` frames (payload the full serialized `Task`) to the single winning node, and SHALL send `kTaskProbeCancel` frames to relinquish nodes. The node SHALL send `kTaskPull` frames (payload `TaskPull{id}`) to claim a task and `kTaskDecline` frames (payload `TaskDecline{id, reason}`) to refuse a probe. Directions: `kTaskProbe`, `kTaskProbeCancel`, `kTaskGrant` are gateway→node; `kTaskPull`, `kTaskDecline` are node→gateway. A node SHALL only send `kTaskDecline` before it has pulled (pre-commit refusal); after pulling, the only exits are grant (run) or cancel (release).

#### Scenario: Probe carries requirements, deps, but not the body

- **WHEN** a gateway sends a `kTaskProbe` for a task with a non-empty body, requirements `{"cpu": 2}`, and `deps = [{source: "ray", key: "obj_abc"}]`
- **THEN** the frame's `TaskProbe` SHALL carry `id`, `type`, `requirements`, and `deps` and SHALL NOT contain the task body

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

On `kTaskProbe` receipt, the node SHALL: admit the task immediately if capacity is available and send `kTaskPull`; otherwise enqueue the probe in its bounded queue for later activation; if the queue is full, SHALL send `kTaskDecline{reason:"queue full"}`; if the task's requirements are permanently unsatisfiable (undeclared pool or requirements exceeding a pool total), SHALL send `kTaskDecline{reason:"requirements unsatisfiable"}` instead of enqueueing. The node SHALL initiate data dependency prefetching for all deps declared in the probe via the `DataDependencyFetcherRouter` at enqueue time. The node SHALL walk the queue oldest-first on admission-relevant events (probe arrival, `CAPACITY_RELEASED`, `DEP_COMPLETED`), admitting and pulling every probe that now fits AND whose deps are all cached, up to a configured concurrency cap. `CAPACITY_RELEASED` arrives via the scheduler's `ProcessCommand` (pure wakeup, `args_ == nullptr`); the node SHALL re-derive capacity from the `AdmissionController` and its own queue. `DEP_COMPLETED` arrives with `args_` pointing to a task id; the node SHALL re-evaluate whether that task's deps are now cached and whether it can be pulled.

#### Scenario: Free capacity pulls immediately

- **WHEN** a probe arrives and `Admit` succeeds
- **THEN** the node SHALL send `kTaskPull` and hold the reservation

#### Scenario: Exhausted pool enqueues the probe and starts prefetch

- **WHEN** a probe arrives, `Admit` returns ResourceExhausted, the queue has room, and `deps` is non-empty
- **THEN** the node SHALL enqueue the probe, SHALL NOT pull yet, and SHALL initiate prefetching for the declared deps

#### Scenario: Full queue declines

- **WHEN** a probe arrives and the bounded queue is at capacity
- **THEN** the node SHALL send `kTaskDecline{reason:"queue full"}` and SHALL NOT enqueue

#### Scenario: Unsatisfiable requirements decline without queueing

- **WHEN** a probe's requirements reference an undeclared pool or exceed a pool total
- **THEN** the node SHALL send `kTaskDecline{reason:"requirements unsatisfiable"}` and SHALL NOT enqueue

#### Scenario: Release drains the queue

- **WHEN** a `CAPACITY_RELEASED` command reaches the scheduler and queued probes now fit available capacity
- **THEN** the node SHALL admit and pull the queued probes in FIFO order up to its concurrency cap

#### Scenario: Dep-cached probe pulled on walk

- **WHEN** `walk()` encounters a queued probe whose capacity fits AND whose deps are all cached
- **THEN** the node SHALL admit and pull the probe

#### Scenario: Deps-not-cached probe stays queued

- **WHEN** `walk()` encounters a queued probe whose capacity fits but whose deps are not all cached
- **THEN** the node SHALL keep the probe queued and SHALL NOT pull it

#### Scenario: Deps-cached triggers walk

- **WHEN** a `DEP_COMPLETED` command reaches the scheduler for a task id whose probe is queued
- **THEN** the node SHALL re-evaluate that probe's readiness (capacity + deps cached) and pull if both conditions are met

#### Scenario: No-deps probe skips prefetch

- **WHEN** a probe arrives with an empty `deps` list
- **THEN** the node SHALL NOT initiate any prefetching and SHALL treat the probe as having zero prefetchable dependencies (Phase-2 behavior)
