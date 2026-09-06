## 1. Wire and protocol schemas

- [x] 1.1 Add probe TLV constants to `TlvFrame` in `src/common/core/io/tlv_frame.hh`: `kTaskProbe=6`, `kTaskProbeCancel=7`, `kTaskPull=8`, `kTaskGrant=9`, `kTaskDecline=10`, with directional doc comments and the existing 0–5 values untouched
- [x] 1.2 Add `api/common/task/probe.proto` (package `strij.task`) with `TaskProbe{id,type,requirements}`, `TaskPull{id}`, `TaskProbeCancel{id}`, `TaskDecline{id,reason}` and its BUILD target; grant reuses `Task`
- [x] 1.3 Add round-trip tests for the probe messages (parse/serialize per spec scenarios)

## 2. RunTask seam (preallocated-capacity variant)

- [x] 2.1 Add the scope-carrying variant to the public `RunTaskService` interface (`include/strij/nodeagent/run_task_service.hh`)
- [x] 2.2 Implement the variant in `RunTaskServiceImpl` (`src/nodeagent/core/run_task_service.{hh,cc}`): skip Admit and kTaskRejected; wrap the handler with `AdmissionTrackingSender(ConnectionResultSender(conn.Mailbox()), std::move(scope))`
- [x] 2.3 Update the service tests/mocks for the new interface method (existing `RunTask` behavior assertions unchanged)

## 3. Nodeagent probe local scheduler

- [x] 3.1 Create the probe scheduler skeleton in `src/nodeagent/extensions/schedulers/probe/`: `ProbeLocalScheduler` + `ProbeLocalSchedulerFactory` (name `"probe"`, `RequiredProtocol()=="probe"`), owned frame types `{kTaskProbe, kTaskProbeCancel, kTaskGrant}`, config proto `ProbeSchedulerConfig{queue_capacity, max_concurrent_preallocations}`
- [x] 3.2 Implement `HandleFrame(kTaskProbe)`: walk → `Admit` ok → preallocate + `kTaskPull`; ResourceExhausted → enqueue (or `kTaskDecline{"queue full"}` when full); FailedPrecondition → `kTaskDecline{"requirements unsatisfiable"}` without enqueueing
- [x] 3.3 Implement the bounded queue (`strij::utils::BoundedQueue<QueuedProbe>`) retaining each probe's `OutboundMailbox`; `ProcessCommand(CAPACITY_RELEASED)` walks it FIFO with the `max_concurrent_preallocations` cap
- [x] 3.4 Implement `HandleFrame(kTaskGrant)`: run the task via the scope-carrying `RunTask` variant with the held scope; grant for an id with no held reservation → log and drop
- [x] 3.5 Implement `HandleFrame(kTaskProbeCancel)`: release the held preallocation and/or drop the queued entry; idempotent for unknown/released ids
- [x] 3.6 Register the scheduler as a capacity observer (`AdmissionController::RegisterCapacityObserver(this)`) in the factory `Create()`; confirm `BuildNodeCapabilities` advertises `"probe"` automatically
- [x] 3.7 Add node-side unit tests: immediate pull on free capacity; enqueue on exhausted pool; decline on full queue; decline on unsatisfiable requirements; FIFO release-walk with concurrency cap; grant runs via the scope variant; cancel releases (idempotent); stray grant dropped; spurious wakeup is a no-op; observer registration happens at construction

## 4. Gateway probe scheduler

- [x] 4.1 Create the probe scheduler skeleton in `src/gateway/extensions/schedulers/probe/`: `ProbeScheduler` + `ProbeSchedulerFactory` (name `"probe"`, `RequiredProtocol()=="probe"`), owned frame types `{kTaskPull, kTaskDecline}`, config proto `ProbeRoleSchedulerConfig{candidate_count, probe_deadline_ms}`
- [x] 4.2 Implement `Schedule`: effective `k = min(candidate_count, GetCandidates("probe").size())` (default `candidate_count=2`); none → `DeliverError`; sample without replacement, deterministic in `task.id`; store per-task state {full Task, receiver, probed nodes, deadline} keyed by `task.id`; send `kTaskProbe` to each candidate
- [x] 4.3 Implement `HandleFrame(kTaskPull)`: first-pull-wins → grant winner (full `Task`), `kTaskProbeCancel` to the other probed nodes, transfer receiver into `ResultReceiverStorage` with the winner's node id, erase probe state; a pull for a granted/unknown id → `kTaskProbeCancel` only
- [x] 4.4 Implement `HandleFrame(kTaskDecline)`: drop the declining candidate; when all candidates have declined → `DeliverError(reason)` + erase probe state (no deadline wait)
- [x] 4.5 Implement the probe deadline with a `PeriodicTimer` sweep: expired tasks → `kTaskProbeCancel` to all outstanding probed nodes, `DeliverError`, erase state
- [x] 4.6 Add gateway-side unit tests: candidate sampling bounds + clamp (k=1 on single node); deterministic per-task sampling; no-eligible immediate error; first-pull-wins; losers canceled at grant; late pull revoked (no double grant); all-declined immediate error; deadline expiry errors the receiver; receiver transfer to storage at grant; every-task-resolves
- [x] 4.7 Add startup validation: reject `candidate_count < 1` in the probe factory config parse

## 5. Config, integration, verification

- [x] 5.1 Verify gateway + nodeagent config plumbing needs no changes beyond the new extension names (generic `ExtensionConfig`/`SchedulerConfig` unpacking) and document a probe scheduler config example (nodeagent `schedulers: [{name: "probe", ...}]`, gateway `schedulers: [{task_type: "", extension: {name: "probe", ...}}]`)
- [x] 5.2 Confirm existing push/round-robin tests pass unchanged, demonstrating zero behavior change to v1 paths
- [x] 5.3 `openspec sync-specs` to fold the delta requirements into main specs (`probe-scheduling`, `typed-tlv-messages`, `task-protocol`, `node-local-scheduler`)
- [x] 5.4 Run `make test` and `make check` (include purity + namespace coherence) and fix any fallout