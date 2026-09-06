## Why

v1 scheduling is push-based: the gateway picks a node (`round_robin`/`capability_aware`) from potentially stale state and pushes the entire task. At cluster scale gateways cannot track cluster-wide resource state, so blind pushes into full nodes become the norm — tasks get rejected or queue pointlessly. Phase 0/1 built the seams (shared `Scheduler` contract, frame routing, `CAPACITY_RELEASED` wakeups, `BoundedQueue`) but nothing consumes them yet. This change lands the Sparrow-style probe protocol: the gateway samples a small candidate set, nodes claim tasks against their own *live* capacity, and self-selection replaces guessing.

## What Changes

- **Probe wire frames**: five new global TLV ids — `kTaskProbe`, `kTaskProbeCancel` (gateway→node), `kTaskPull` (node→gateway), `kTaskGrant`, `kTaskDecline` — with `TaskProbe`/`TaskPull`/`TaskProbeCancel`/`TaskDecline` protobuf messages and the existing `Task` reused for the grant body.
- **Nodeagent probe local scheduler** (`"probe"`): admits-and-pulls on probe receipt when capacity is free; otherwise enqueues in a `BoundedQueue`; on `CAPACITY_RELEASED` walks the queue FIFO, preallocates (`Admit`) and pulls. Full queue or permanently-unsatisfiable requirements → immediate `kTaskDecline`. `kTaskGrant` runs the task via a new preallocated-capacity `RunTask` variant; `kTaskProbeCancel`/`kTaskDecline` release the preallocation and rely on the resulting release broadcast to drain the queue.
- **Gateway probe scheduler** (`"probe"`): power-of-two sampling over `GetCandidates(RequiredProtocol())`, first-`kTaskPull`-wins race arbiter (grant winner, cancel remaining probed nodes), per-task deadline with a cancel sweep, and immediate `DeliverError` when no node is eligible or all candidates decline. The receiver lives in the scheduler's probe state until grant, then transfers to `ResultReceiverStorage` (node I/O and result routing unchanged).
- **`RunTaskService` seam**: new variant that runs a task under a caller-supplied admission scope (no double-`Admit`), so the probe's held preallocation becomes the task's reservation. **BREAKING** to the `RunTaskService` interface (additive virtual; new impl required).
- **Config**: nodeagent and gateway `schedulers` entries gain `probe` extension configs (queue capacity / admission policy; candidate count / probe deadline). Existing `push`+`round_robin` behavior is untouched.

## Capabilities

### New Capabilities
- `probe-scheduling`: the two-sided probe protocol — message exchange semantics (probe/pull/grant/decline/cancel), node-side deferred admission (queue, preallocation, wakeup walk, decline on full), gateway-side arbitration (sampling, first-pull-wins, cancel-losers, deadline), receiver lifetime, and the `probe` scheduler registrations.

### Modified Capabilities
- `typed-tlv-messages`: five new TLV `type_id` constants (`kTaskProbe`=6 … `kTaskDecline`=10) and their frame payload contracts.
- `task-protocol`: new probe message schemas (`TaskProbe`, `TaskPull`, `TaskProbeCancel`, `TaskDecline`); `kTaskGrant` reuses `Task`.
- `node-local-scheduler`: `RunTask` service gains the preallocated-capacity variant; probe scheduler registers as capacity observer.

## Impact

- `src/common/core/io/tlv_frame.hh` — new `type_id` constants.
- `api/common/task/probe.proto` (new) — probe message schemas.
- `include/strij/nodeagent/run_task_service.hh`, `src/nodeagent/core/run_task_service.*` — new scope-carrying run variant.
- `src/nodeagent/extensions/schedulers/probe/*` (new), `src/gateway/extensions/schedulers/probe/*` (new) — the two scheduler halves, factories, config protos.
- Routing seams unchanged: `NodeagentTlvHandler` and `SchedulerRouter`/`GatewayTlvHandler` already dispatch by `HandledFrameTypes()`; the probe schedulers simply register their frame types. `BuildNodeCapabilities` picks up `"probe"` automatically via `RequiredProtocol()`.
- Tests: new unit tests for both scheduler halves; existing push/round-robin tests unchanged (zero behavior change to v1 paths).