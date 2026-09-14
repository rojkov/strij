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
- A local child runs on the node when capacity allows, delivering results to a node-local receiver registry; otherwise it is forwarded to a gateway and its results return via the two-hop path.
- The parent cannot distinguish local from remote execution ("local vs remote is indistinguishable to the parent").
- No wire-protocol change: upstream children reuse `kTaskSubmission`; outcomes reuse `kResult` / `kTaskRejected`.
- Node-side config declares local scheduling authority explicitly (`task_type` claims + a single `local_default`), deliberately diverging from the gateway's empty-means-default shape; a `gateway_client` section configures egress.

**Non-Goals:**
- Probe-based child scheduling (children are deliberately *not* probed; the policy is run-immediately-if-capacity-else-forward).
- Deadlines/TTLs and probe cancellation hardening (Phase 5 of the archived plan).
- A production workflow handler — the change builds the mechanism and an example workflow handler for tests/validation.
- Parent linkage in `Task` (`parent_id`) — deferred; the registry keys on the child id alone.

## Decisions

### D1: Submitter reaches handlers at construction, not via `HandleTask` (locked)

`HandleTask(const task::Task&, ResultSenderPtr)` does **not** change. The sender is per-*call* because it is per-*task* (bound to a task instance and its connection); the submitter is a shared node *service* — the same category as `RunTaskService`, `FunctionResolver`, `AdmissionController`, `DataDependencyFetcherRouter`, all of which reach extensions at construction through the factory context. `NodeagentFactoryContext` exposes a `ChildTaskSubmitter&`; a workflow handler's `TaskHandlerFactory::Create(config, context)` retrieves it and passes it to the handler's constructor. `echo` and `piped_executable` never request it; their constructor signatures and tests are untouched.

This also removes a dependency knot the ctx-struct alternative would have created: `RunTaskService` builds the per-call context, so it would need to *hold the child router* — but the child router is a composite over local schedulers, which are built around `RunTaskService` (a cycle requiring another two-phase `SetXxx` init). Keeping the submitter out of the per-call path leaves `run_task_service.cc` untouched by the interface change.

- *Alternative considered*: uniform `TaskExecutionContext{sender, submitter}` passed to every `HandleTask` (archived design). Rejected: churns every handler + handler test for a capability most handlers will never use; forces the cycle above; violates the codebase's fail-fast service-injection doctrine (D2 of the archived design rejected a kitchen-sink context for the same reason).
- *Alternative considered*: optional second virtual (`HandleTaskCtx`) implemented only by workflow handlers. Rejected: `RunTaskService` would need a dynamic capability probe per task; two parallel invocation paths.
- *Note*: future per-submission hints (e.g. locality) are arguments on the submitter, not a different handle.

### D2: One node-side child router; per-type authority with an explicit local default

Node-side local schedulers are primarily wire-protocol counterparts to the gateway schedulers (their frame handling — `kTaskSubmission`, probe frames). Their node-local `Schedule` facet is a separate, opt-in concern, so the node's entry shape deliberately **diverges from the gateway's**: a `task_type` and a "local default" are explicit and independent declarations, not an empty-`task_type`-means-default encoding.

- `task_type` (optional) — the entry is authoritative over locally-originated tasks of that type.
- `local_default` (optional bool, at most one entry) — the entry is the node's fallback authority for locally-originated tasks no other entry claims.
- neither — the entry schedules **no** locally-originated tasks at all (pure wire-protocol counterpart). An empty `task_type` on the node does NOT select a default.

A node-side `ChildSchedulerRouter` (implementing `extensions::Scheduler`, and the single submission handle a workflow handler holds) dispatches `Schedule(child, receiver)` by `child.type()` to the declaring entry, falls back to the single `local_default` entry, and delivers an error when no entry claims the type and no local default is declared. This reconciles the archived plan's two statements ("a single submission scheduler — the node's capacity authority" vs "child tasks route to the scheduler owning that type"): **the router is the single submission handle; routing is by declared authority; the explicitly-marked local default owns the fallback.**

The local scheduler's role in a child submission is thin and identical across `push` and `probe`: *try to admit locally; if admitted, run locally; else forward*. The shared part is a core `ChildSubmissionService` (enters the context) that performs the admit-or-forward step; each authority-holding constituent's `Schedule` delegates to it. `probe`'s child path specifically does **not** queue: a child is local, so the probe dance is pointless.

### D3: Local child run — sender-backed `RunTask` overloads

`RunTaskService` gains two overloads without a `Connection`:
- `RunTask(const task::Task&, ResultSenderPtr sender)` — admitting path, replaces the `ConnectionResultSender` with the caller's sender.
- `RunTask(const task::Task&, ResultSenderPtr sender, AdmissionScopePtr reserved)` — preallocated path.

The existing `io::Connection`-bound overloads remain as thin wrappers (building `ConnectionResultSender`), so the push path is byte-identical. The local child's sender is a `RegistryResultSender { LocalReceiverRegistry&, task_id }`: `Send(TaskResult)` looks up the parent's receiver by id, delivers `body`/`is_final`, and `Erase`s the registry entry on the final result.

```
parent handler            child router / ChildSubmissionService
   │ Submit(child, recv)  │
   │─────────────────────►│
   │  MakeReceiver → registry.Put(child_id, recv)
   │                      │ Admit(child) ok?
   │                      │  yes → RunTask(child, RegistryResultSender(registry, child_id))
   │                      │  no  → ChildSubmissionService.Forward → GatewayClient (D5)
   │                      │
   │  ◄── final result ───┼── registry.Get(child_id).Deliver → erases entry
```

### D4: Local receiver registry + inbound child-outcome routing

A node-side `LocalReceiverRegistry` mirrors `gateway::ResultReceiverStorage`: `Put(task_id, ReceiverPtr)`, `Get(task_id)`, `Erase(task_id)`, `Size/Empty`. `NodeagentTlvHandler` gains two **core** frame cases (it already owns core responsibilities such as `kNodeAdvertisement`): inbound `kResult` and `kTaskRejected` on any node connection are looked up by id in the registry and delivered (`Deliver`/`DeliverError`), erased on final. Unknown ids are dropped with a warning — the frames are core-owned, *not* scheduler-owned, keeping the protocol frame-ownership doctrine intact (a `type_id` has exactly one owner, and result routing is not a scheduling protocol's business).

Receiver lifetime: the registry entry died with the parent (handler-erased on its own connection close, via `ResultSender::RegisterOnClose`) or on the final result / rejection. A parent that dies without cleanup leaks an entry until TTL hardening (Phase 5); Phase 4 accepts a bounded, logged leak and documents the `RegisterOnClose` recipe for handlers.

### D5: Forward path — `GatewayClient` egress

A node `GatewayClient` (enters `NodeagentFactoryContext`) is the node's only outbound capability and supports the *forward* half of the child policy:

- The node's `TcpListener` registers every accepted connection with the `GatewayClient` (currently all inbound peers are gateways by construction). `Submit(task, receiver)` round-robins over the live registered connections and writes an upstream `kTaskSubmission` frame on one of them.
- When no live connection exists, it dials a configured address from `NodeAgentConfig.gateway_client.addresses` (async `PrepareConnect`; the submission is queued until the connection completes), falling back across addresses and finally delivering an error to the parent receiver when none is reachable.
- The child's result arrives back on the same connection (gateway → node `kResult`), regardless of which connection carried the upstream submission.

```
nodeA                                      gateway G
   │── upstream kTaskSubmission ──────────►│  (on the gateway-dialed connection)
   │                                       │  default seam → router claims kTaskSubmission
   │                                       │  Schedule(child, NodeConnectionResultReceiver(conn))
   │                                       │  storage.Put(child_id, nodeA)
   │   ◄── kResult on same connection ─────│  (two-hop: worker → gateway → nodeA → registry → parent)
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

The scheduler entry shape change is a **breaking** nodeagent config change (no legacy `ExtensionConfig`-shaped loading). It is a distinct `NodeSchedulerConfig` message, NOT a reuse of the gateway `SchedulerConfig`, because `task_type` means "local authority" here (empty ≠ default) and the explicit `local_default` marker has no gateway analogue. `gateway_client` is additive. With no `gateway_client.addresses` configured, forwarding still works over live connections but silent-dials nothing when none are live. A node whose entries declare no local roles cannot originate tasks (its `Schedule`/submit path always errors).

## Sequence diagrams

Local child run:

```
parent handler        ChildSchedulerRouter        owning local scheduler / ChildSubmissionService
   │  Submit(child, recv)                          │
   │───────────────────────►  type → owning sched  │
   │                        │  Schedule(child,recv)│
   │                        │─────────────────────►│  Admit ok
   │                        │                     │── RunTask(child, RegistryResultSender(registry, id))
   │                        │                     │   handler.Send(final) → registry → parent → Erase
   │  ◄── result ───────────┼─────────────────────│
```

Forwarded child (two-hop):

```
nodeA (parent)          gateway G                    nodeB (worker)
   │ Submit(child,recv) │                            │
   │ registry.Put(id)   │                            │
   │ upstream kTaskSubmission ──► router.claim(kSub) │
   │                      Schedule(child, NodeRecv)  │
   │                      storage.Put(id, nodeA)     │
   │                      └── kTaskSubmission ──────►│  RunTask admits + runs
   │                          ◄── kResult ──────────│  (final)
   │                      storage.Get → NodeRecv     │
   │  ◄── kResult on nodeA↔G conn ──────────────────│
   │  dispatcher → LocalReceiverRegistry → parent   │
```

## Risks / Trade-offs

- **Rebound loop** — a forwarded child can be scheduled by the gateway back to the saturated submitting node; push admission fails → `kTaskRejected` → parent error, i.e. the forward was wasted but terminates (push never retries). Mitigation: accepted in Phase 4; probe-based child scheduling is the later refinement.
- **No live connection and unreachable configured gateways** → the parent receiver gets an error. Mitigation: address fallback + clear error text; documented behavior.
- **Orphaned registry entries** — a parent handler that dies without erasing its children leaks registry entries until TTL hardening. Mitigation: Phase-4 handler recipe erases on its own `RegisterOnClose`; logged leak accepted.
- **`NodeagentTlvHandler` gains core cases** (churn in the dispatcher constructor) — the frame-ownership doctrine stays intact because result frames are core, not protocol, frames.
- **Two-phase construction in the nodeagent binary** — `ChildSchedulerRouter` needs the local schedulers; `GatewayClient` needs the context; the registry is standalone. Wiring order matters but is linear (no cycle) because of D1.
- **Multiple gateways** — upstream submission is round-robin over live connections; two-hop routing is connection-agnostic, so results return correctly even across different connections.

## Migration Plan

1. **Phase 4A (node, land first)** — `NodeagentFactoryContext.ChildTaskSubmitter` + `ChildSchedulerRouter` + `LocalReceiverRegistry` + `RegistryResultSender` + sender-backed `RunTask` overloads + inbound `kResult`/`kTaskRejected` core cases + example workflow handler + `schedulers` config shape change. Self-contained; egress absent (forwarding is disabled, so capacity-deficient children error — preserving the Phase-0 behavior of a node that cannot enlist remote capacity). **BREAKING config**: `schedulers` entry shape.
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
- **Registry eviction** beyond final-result/erasure — TTLs deferred to Phase 5 hardening.