## Context

The pluggable-scheduling pipeline (archived `2026-08-30-pluggable-task-scheduling`) delivered the mechanism but left the nodeagent's `Schedule` facet reserved: both `PushLocalScheduler::Schedule` and the probe scheduler's deliver an error to the receiver (`node-local-scheduler` spec, "Child-task Schedule is reserved"). The architecture's own Phase 4 sketch (`design.md:193-204`) describes workflow handlers spawning child tasks whose results return to the parent, whether the child runs locally or is forwarded to a gateway. That sketch assumed `HandleTask` would evolve into an invocation-context struct bundling sender + submission handle.

This change locks a deviation: **the `TaskHandler` interface stays `HandleTask(const Task&, ResultSenderPtr)`**. The submitter is a shared node service delivered at handler construction via `NodeagentFactoryContext`, exactly like `RunTaskService`, `FunctionResolver`, `AdmissionController`, and `DataDependencyFetcherRouter` today. Only handlers that drive stateful workflows need it; `echo` and `piped_executable` never know it exists.

Existing infrastructure this change leans on:
- The node's frame dispatcher routes by TLV `type_id`; per-protocol ownership is disjoint.
- `AdmissionTrackingSender` wraps *any* `ResultSenderPtr` (`admission_tracking_sender.hh:24`), so a local-child sender slots in without touching capacity accounting.
- The gateway's `SchedulerRouter` is a composite `Scheduler` with per-`task_type` dispatch + default fallback (`scheduler_router.hh`).
- `GatewayTlvHandler` already hands unclaimed frame types to the router via its default seam (`gateway_tlv_handler.cc:38-41`); `ResultReceiverStorage` already keys receivers by `task_id` and `node_id`, with disconnect cleanup (`result_receiver_storage.hh`).
- The node is currently a pure server: `TcpListener` accepts gateway dials; there is no outbound facility anywhere on the nodeagent.

## Goals / Non-Goals

**Goals:**
- Activate the node's `Schedule` facet for child tasks with one shared submission handle; keep `HandleTask` and `RunTask` free of the submitter (the locked interface decision).
- A locally originated child task runs on the node when capacity allows, delivering results to a node-local result-receiver storage; otherwise it is forwarded to a gateway and its results return via the two-hop path.
- The parent task cannot distinguish local from remote execution ("local vs remote is indistinguishable to the parent").
- No wire-protocol change: upstream children reuse `kTaskSubmission`; outcomes reuse `kResult` / `kTaskRejected`.
- Node-side config declares local scheduling authority explicitly (`task_type` claims + a single `local_default`), deliberately diverging from the gateway's empty-means-default shape; a `gateway_client` section configures egress.

**Non-Goals:**
- Probe-based child scheduling (children are deliberately *not* probed; the policy is run-immediately-if-capacity-else-forward).
- Deadlines/TTLs and probe cancellation hardening (Phase 5 of the archived plan).
- A production workflow handler — the change builds the mechanism and an example workflow handler for tests/validation.
- Parent linkage in `Task` (`parent_id`) — deferred; the storage keys on the child id alone.

## Decisions

### D1: Submitter reaches handlers at construction, not via `HandleTask` (locked)

`HandleTask(const task::Task&, ResultSenderPtr)` does **not** change. The sender is per-*call* because it is per-*task* (bound to a task instance and its connection); the submitter is a shared node *service* — the same category as `RunTaskService`, `FunctionResolver`, `AdmissionController`, `DataDependencyFetcherRouter`, all of which reach extensions at construction through the factory context. `NodeagentFactoryContext` exposes a `ChildTaskSubmitter&`; a workflow handler's `TaskHandlerFactory::Create(config, context)` retrieves it and passes it to the handler's constructor. `echo` and `piped_executable` never request it; their constructor signatures and tests are untouched.

This also removes a dependency knot the ctx-struct alternative would have created: `RunTaskService` builds the per-call context, so it would need to *hold the child router* — but the child router is a composite over local schedulers, which are built around `RunTaskService` (a cycle requiring another two-phase `SetXxx` init). Keeping the submitter out of the per-call path leaves `run_task_service.cc` untouched by the interface change.

- *Alternative considered*: uniform `TaskExecutionContext{sender, submitter}` passed to every `HandleTask` (archived design). Rejected: churns every handler + handler test for a capability most handlers will never use; forces the cycle above; violates the codebase's fail-fast service-injection doctrine (D2 of the archived design rejected a kitchen-sink context for the same reason).
- *Alternative considered*: optional second virtual (`HandleTaskCtx`) implemented only by workflow handlers. Rejected: `RunTaskService` would need a dynamic capability probe per task; two parallel invocation paths.
- *Note*: future per-submission hints (e.g. locality) are arguments on the submitter, not a different handle.

### D2: One node-side child task router; per-type authority with the bundled `default` local authority

Node-side local schedulers are primarily wire-protocol counterparts to the gateway schedulers (their frame handling — `kTaskSubmission`, probe frames). Their node-local `Schedule` facet is a separate concern, so the shape of a `schedulers` config entry deliberately **diverges from the gateway's**: a `task_type` and a "local default" are explicit and independent declarations, not an empty-`task_type`-means-default encoding.

- `task_type` (optional) — the local scheduler is authoritative over locally-originated tasks of that type.
- `local_default` (optional bool, at most one local scheduler) — the local scheduler is the node's fallback authority for locally-originated tasks no other local scheduler claims.
- neither — the local scheduler schedules **no** locally-originated tasks at all (pure wire-protocol counterpart). An empty `task_type` on the node does NOT select a default.

A node-side `NodeagentSchedulerRouter` (implementing `extensions::Scheduler`, and the single submission handle a workflow handler holds) dispatches `Schedule(child, receiver)` by `child.type()` to the declaring local scheduler, falls back to the single local scheduler marked `local_default`, and delivers an error when no local scheduler claims the type and no local default is declared. This reconciles the archived plan's two statements ("a single submission scheduler — the node's capacity authority" vs "child tasks route to the scheduler owning that type"): **the router is the single submission handle; routing is by declared authority; the explicitly-marked local default owns the fallback.**

The child policy lives in **one place**: the bundled `default` scheduler (`DefaultLocalScheduler`, factory name `"default"`), whose config entry is configured with `local_default: true`. Its `Schedule` is the admit-or-forward step: register the receiver in its own `LocalResultReceiverStorage` first, then *try to admit locally; if admitted, run locally; else forward*. The push/probe schedulers' `Schedule` facets are **Unimplemented** — they log a warning and deliver an error through the receiver (never hang), because a node must not carry two competing local authorities. `probe`'s child path specifically does **not** queue: a child is local, so the probe dance is pointless; `probe` remains a pure wire-protocol counterpart.

### D3: Local child run — sender-backed `RunTask` overloads

`RunTaskService` gains two overloads without a `Connection`:
- `RunTask(const task::Task&, ResultSenderPtr sender)` — admitting path, replaces the `ConnectionResultSender` with the caller's sender.
- `RunTask(const task::Task&, ResultSenderPtr sender, AdmissionScopePtr reserved)` — preallocated path.

The existing `io::Connection`-bound overloads remain as thin wrappers (building `ConnectionResultSender`), so the push path is byte-identical. The local child's sender is a `StorageResultSender { LocalResultReceiverStorage&, task_id }`: `Send(TaskResult)` looks up the parent's receiver by id, delivers `body`/`is_final`, and `Erase`s the storage entry on the final result.

```
parent handler          child router            default scheduler
   │ Submit(child, recv)│                       │
   │───────────────────►│ type → default        │
   │                    │  Schedule(child,recv) │
   │                    │──────────────────────►│ storage.Put(child_id, recv)
   │                    │                       │ Admit(child) ok?
   │                    │                       │  yes → RunTask(child, StorageResultSender(storage, child_id))
   │                    │                       │  no  → Forward → GatewayClient (D5), or DeliverError
   │                    │                       │
   │  ◄── final result ─┼───────────────────────┼── storage.Get(child_id).Deliver → erases entry
```

### D4: Local result receiver storage + child-outcome frames owned by the default scheduler

A node-side `LocalResultReceiverStorage` mirrors `gateway::ResultReceiverStorage`: `Put(task_id, ReceiverPtr)`, `Get(task_id)`, `Erase(task_id)`, `Size/Empty`. Unlike the gateway — where the `SchedulerRouter` owns `kTaskSubmission` and `ResultReceiverStorage` lives in the core — the node's child-outcome routing is a *scheduling* concern owned by the bundled `default` scheduler: it holds the storage privately and claims **both** `kResult` and `kTaskRejected` in its `HandledFrameTypes()`. The `NodeagentSchedulerRouter` is the frame demux (mirroring the gateway `SchedulerRouter`): `NodeagentTlvHandler` keeps only the core `kNodeAdvertisement` write and a default seam that routes every other frame into `router.HandleFrame(frame, conn)`; the router's `findFrameOwner` dispatches by `type_id` to the claiming scheduler (`default` for the child-outcome frames, `push`/`probe` for their submission/grant frames). Unclaimed types are dropped with a warning on both seams.

`DefaultLocalScheduler::HandleFrame(kResult | kTaskRejected)` looks the id up in its own storage and delivers (`Deliver`/`DeliverError`) — resolving the exact entry `Schedule` created for a forwarded child — erasing on final. Unknown ids are dropped with a warning. The frame-ownership doctrine still holds: a `type_id` has exactly one owner; ownership is resolved at router level, exactly as on the gateway.

Receiver lifetime: the storage entry died with the parent (handler-erased on its own connection close, via `ResultSender::RegisterOnClose`) or on the final result / rejection. A parent that dies without cleanup leaks an entry until TTL hardening (Phase 5); Phase 4 accepts a bounded, logged leak and documents the `RegisterOnClose` recipe for handlers.

### D5: Forward path — `GatewayClient` egress

A node `GatewayClient` (enters `NodeagentFactoryContext`) is the node's only outbound capability and supports the *forward* half of the child policy:

- The node's `TcpListener` registers every accepted connection with the `GatewayClient` (currently all inbound peers are gateways by construction). `Forward(task)` round-robins over the live registered connections and writes an upstream `kTaskSubmission` frame on one of them.
- When no live connection exists, `Forward` returns a non-Ok status; the child-policy step turns that into a `DeliverError` to the parent receiver so the parent never hangs. (Outbound dial fallback across `gateway_client.addresses` is deferred: the section is additive config only for now.)
- The child's result arrives back on the same connection (gateway → node `kResult`), regardless of which connection carried the upstream submission.

```
nodeA                                      gateway G
   │── upstream kTaskSubmission ──────────►│  (on the gateway-dialed connection)
   │                                       │  default seam → router claims kTaskSubmission
   │                                       │  Schedule(child, NodeConnectionResultReceiver(conn))
   │                                       │  storage.Put(child_id, nodeA)
   │   ◄── kResult on same connection ─────│  (two-hop: worker → gateway → nodeA → storage → parent)
```

### D6: Gateway inbound child handling

The gateway treats an upstream `kTaskSubmission` as a normal task:

- `SchedulerRouter::HandledFrameTypes()` gains `kTaskSubmission`; `HandleFrame` parses the `Task`, builds a `NodeConnectionResultReceiver` bound to the submitting `conn`, and calls `Schedule(task, receiver)`. The existing per-type dispatch applies (type match, default fallback, no-match error).
- `NodeConnectionResultReceiver` is the TLV sibling of `HttpResultReceiver`: `Deliver(body, is_final)` serializes a `TaskResult` and writes a `kResult` frame over the connection's mailbox; `DeliverError(reason)` writes a `kTaskRejected` frame. It is stored in the existing `ResultReceiverStorage` with `node_id = owningNode(conn)->id`, so `NotifyNodeDisconnected` cleans up a submitting node's outstanding children for free.

No new gateway wiring path is needed: `GatewayTlvHandler`'s default seam already routes unclaimed frames to the router (`gateway_tlv_handler.cc:38-41`), and disconnect/reject/result handling already exists.

### D7: Config shape

```
NodeAgentConfig.schedulers    → repeated NodeSchedulerConfig {
                                  ExtensionConfig extension;
                                  String task_type;    // optional local authority
                                  bool local_default;  // optional; at most one entry
                                }
                                // ≥1 entry; empty task_type ≠ default;
                                // at most one local_default; unique non-empty task_type
NodeAgentConfig.gateway_client → { repeated String addresses; }
```

The scheduler entry shape change is a **breaking** nodeagent config change (no legacy `ExtensionConfig`-shaped loading). It is a distinct `NodeSchedulerConfig` message, NOT a reuse of the gateway `SchedulerConfig`, because `task_type` means "local authority" here (empty ≠ default) and the explicit `local_default` marker has no gateway analogue. The reference configuration includes the bundled factory `"default"` declared as the sole `local_default` (its `DefaultSchedulerConfig` is empty and needs no `typed_config`); push/probe entries stay as wire-protocol counterparts with no local role. `gateway_client` is additive; with no `gateway_client.addresses` configured (or none configured at all), forwarding still works over live connections and errors when none are live. A node whose entries declare no local roles cannot originate tasks (its `Schedule`/submit path always errors).

## Sequence diagrams

Local child run:

```
parent handler        NodeagentSchedulerRouter     default scheduler
   │  Submit(child, recv)                          │
   │───────────────────────►  type → owning sched  │
   │                        │  Schedule(child,recv)│
   │                        │─────────────────────►│  storage.Put(id) → Admit ok
   │                        │                     │── RunTask(child, StorageResultSender(storage, id))
   │                        │                     │   handler.Send(final) → storage → parent → Erase
   │  ◄── result ───────────┼─────────────────────│
```

Forwarded child (two-hop):

```
nodeA (parent)          gateway G                    nodeB (worker)
   │ Submit(child,recv) │                            │
   │ default: storage.Put(id)                       │
   │ upstream kTaskSubmission ──► router.claim(kSub) │
   │                      Schedule(child, NodeRecv)  │
   │                      storage.Put(id, nodeA)     │
   │                      └── kTaskSubmission ──────►│  RunTask admits + runs
   │                          ◄── kResult ──────────│  (final)
   │                      storage.Get → NodeRecv     │
   │  ◄── kResult on nodeA↔G conn ──────────────────│
   │  handler seam → router → default scheduler's   │
   │  storage → parent                             │
```

## Risks / Trade-offs

- **Rebound loop** — a forwarded child can be scheduled by the gateway back to the saturated submitting node; push admission fails → `kTaskRejected` → parent error, i.e. the forward was wasted but terminates (push never retries). Mitigation: accepted in Phase 4; probe-based child scheduling is the later refinement.
- **No live connection and unreachable configured gateways** → the parent receiver gets an error. Mitigation: address fallback + clear error text; documented behavior.
- **Orphaned storage entries** — a parent handler that dies without erasing its children leaks storage entries until TTL hardening. Mitigation: Phase-4 handler recipe erases on its own `RegisterOnClose`; logged leak accepted.
- **`NodeagentTlvHandler` becomes a thin seam** — the child-outcome cases move out of the node core into the bundled `default` scheduler (mirroring the gateway, where the `SchedulerRouter` owns `kTaskSubmission`). The frame-ownership doctrine stays intact: a `type_id` has exactly one owner, and the router resolves ownership by `HandledFrameTypes()` at demux time. The handler keeps only advertisement + the router seam.
- **Two-phase construction in the nodeagent binary** — the `default` scheduler needs `RunTaskService` + `AdmissionController` + the `GatewayClient` forwarder; `GatewayClient` needs the context's connections. Wiring order matters but is linear (no cycle) because of D1: `GatewayClient` is installed as the `ChildTaskForwarder` **before** the schedulers are built.
- **Multiple gateways** — upstream submission is round-robin over live connections; two-hop routing is connection-agnostic, so results return correctly even across different connections.

## Migration Plan

1. **Phase 4A (node, land first)** — `NodeagentFactoryContext.ChildTaskSubmitter` + `NodeagentSchedulerRouter` (now also the frame demux) + bundled `default` scheduler owning `LocalResultReceiverStorage` + router-owned child-outcome cases + `StorageResultSender` + sender-backed `RunTask` overloads + example workflow handler + `schedulers` config shape change (`default` with `local_default: true`). Self-contained; egress absent (forwarding is disabled, so capacity-deficient children error — preserving the Phase-0 behavior of a node that cannot enlist remote capacity). **BREAKING config**: `schedulers` entry shape.
2. **Phase 4B (forward)** — `GatewayClient` + `gateway_client` config + gateway inbound child routing (`NodeConnectionResultReceiver`, router claims `kTaskSubmission`). Wire-unchanged; node and gateway can roll independently (a gateway rejecting `kTaskSubmission` before upgrade drops them, so the node sees a dropped forward → parent error; upgrade order: gateway first).
3. **Rollback** — revert the affected binary; both halves stay interoperable on the unchanged wire.

## Open Questions

- **Connection selection policy** in `GatewayClient` beyond round-robin (e.g. least-recently-used, per-pool affinity). Default: FIFO round-robin.
- **`parent_id` on `Task`** for observability/workflow tooling — deferred; not required for correct two-hop delivery.

## Deferred Decisions

- **Repeated `task_type` claims**: this change keeps a single optional `task_type` per scheduler config entry on **both** sides. The shapes are designed so `task_type` can become `repeated string task_types` in a later change:
  - node-side `NodeSchedulerConfig` — a single protocol then owns several locally-originated types without duplicate frame-dispatch-colliding entries;
  - gateway-side `SchedulerConfig` — a single scheduling policy (e.g. `round_robin`) then serves several task types without duplicate binder entries.
  Both extensions must validate claim uniqueness across entries when they land.
- **Storage eviction** beyond final-result/erasure — TTLs deferred to Phase 5 hardening.