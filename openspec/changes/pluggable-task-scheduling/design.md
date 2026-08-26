## Context

Both ends of the wire hardcode a single task-flow path today:

- **Gateway** — `GatewayHttpHandler::HandleMessage` calls the configured `extensions::Scheduler::Choose(NodeDirectory&, TaskOffer)` (`gateway_http_handler.cc:100`) and then serializes + writes the `kTaskSubmission` frame itself (`:126`). Exactly one scheduler comes from `GatewayConfig.scheduler`. The `Scheduler` interface is policy-only (`Choose`), with no protocol I/O.
- **Nodeagent** — `NodeagentTlvHandler::HandleFrame` (`nodeagent_tlv_handler.cc:47`) hardcodes the `kTaskSubmission` path: parse `Task` → `TaskHandlerManager::GetHandler(type)` → `AdmissionController::Admit` → run handler with an `AdmissionTrackingSender`. All other inbound frames are silently dropped.
- **Extensions** — one shared `FactoryContext` (Dispatcher, Logger, FunctionResolver) serves every factory category (`TaskHandlerFactory`, `SchedulerFactory`, `NodeDiscoveryFactory`), and `Registry<T>` + `REGISTER_FACTORY_*` macros instantiate factories. `BuildNodeCapabilities` hardcodes `scheduling_protocols = ["push"]` (`capabilities.cc:97`).

The target architecture is probe-based scheduling at cluster scale, deferred admission with bounded queues, data-dependency gating, and locally-originated child tasks — none of which the current policy-only `Choose` model can express. This change (Phase 0) builds the *mechanism*: a single scheduling contract shared by both sides, per-type schedulers on the gateway, a pluggable local scheduler on the node, and the seams future protocols hang off. The wire protocol is untouched.

## Goals / Non-Goals

**Goals:**
- One `Scheduler` interface shared by gateway and nodeagent scheduler extensions: `Schedule`, `RequiredProtocol`, `HandleFrame`, `HandledFrameTypes`.
- Gateway configured with multiple schedulers dispatched by task type, with a default fallback; `GatewayHttpHandler` becomes fire-and-forget `Schedule`.
- Nodeagent local scheduler pluggable; the initial `"push"` local scheduler preserves current behavior byte-for-byte; `NodeagentTlvHandler` becomes a thin dispatcher; the execution path is extracted into a core-owned `RunTask` service.
- Nodeagent schedulers adopt the `event::CommandHandler` role so node-internal events never leak into the shared interface.
- No wire change; existing configs keep working (unset nodeagent scheduler defaults to `push`; legacy single gateway `scheduler` normalizes to a default entry).

**Non-Goals:**
- The probe protocol, the capacity-released event's first consumer, data-dependency fetching, and child-task submission. These are designed-for and sketched below, but not implemented.

## Decisions

### D1: One `Scheduler` interface shared by both sides

```cpp
class Scheduler {
public:
  virtual void Schedule(const task::Task& task, ResultReceiverPtr receiver) PURE;
  [[nodiscard]] virtual auto RequiredProtocol() const -> std::string_view PURE;
  virtual void HandleFrame(io::TlvFrame frame, io::Connection& conn) {}
  [[nodiscard]] virtual auto HandledFrameTypes() const -> std::span<const uint8_t> { return {}; }
};
```

`Choose` is removed; node selection becomes a private helper inside push-style schedulers. Every task handed to `Schedule` must eventually resolve (result or error to the receiver) — the receiver never hangs.

- *Alternative considered*: separate gateway/node scheduler interfaces. Rejected — scheduling is a bidirectional protocol and the fire-and-forget submit + protocol-I/O contract is identical on both ends.
- *Alternative considered*: `Notify*` virtuals for node-internal events on the shared interface. Rejected — leaks nodeagent-only concerns to gateway schedulers (see D6).

### D2: Factory context split, no service leaks

```
FactoryContext (base: Dispatcher, Logger)
 ├── GatewayFactoryContext   (+ NodeDirectory, ResultReceiverStorage)
 └── NodeagentFactoryContext (+ FunctionResolver, AdmissionController, RunTaskService)
```

Each binary provides its own implementation; a scheduler factory fails fast at construction if the context lacks a service it needs. `FunctionResolver` moves out of the shared context into the nodeagent side (the gateway never used it).

- *Alternative considered*: keep one `FactoryContext` with optional accessors. Rejected — leaks gateway services (e.g. `NodeDirectory`) into the nodeagent and invites downcasting.

### D3: Scheduler factory category split

`GatewaySchedulerFactory` (registered in `Registry<GatewaySchedulerFactory>`, `Create(config, GatewayFactoryContext&)`) and `NodeSchedulerFactory` (`Registry<NodeSchedulerFactory>`, `Create(config, NodeagentFactoryContext&)`), both returning `SchedulerPtr`. Two registries because the construction contexts genuinely differ; the runtime contract stays one. The gateway and node halves of the same wire protocol are different classes in different categories.

### D4: Per-type scheduler router on the gateway

```
GatewayHttpHandler ──► SchedulerRouter (implements Scheduler)
                          │  type → SchedulerPtr map + default
                          ▼
                    Schedule(task, receiver)  → dispatch by task.type()
                    unmatched → default (or DeliverError when no default)
```

The router is a composite `Scheduler`; `GatewayHttpHandler` holds only a `Scheduler&` and calls `Schedule` fire-and-forget. `round_robin` and `capability_aware` reimplement their `Choose` bodies as the private selection step inside `Schedule`; when no node is eligible they call `receiver->DeliverError(...)` instead of returning `nullptr`.

### D5: Nodeagent frame dispatcher + `RunTask` service

```
Connection ──► NodeagentTlvHandler (dispatcher)
                  │  type ∈ scheduler.HandledFrameTypes()  ──► scheduler.HandleFrame
                  │  (kNodeAdvertisement still emitted first; kNodeState broadcasting
                  │   stays with StateReporter)
                  ▼
PushLocalScheduler::HandleFrame(kTaskSubmission) ──► RunTask(parsed Task, conn)
                                                        │
   parse → GetHandler(type) → Admit → [AdmissionScope]   │ fail → kTaskRejected
   → AdmissionTrackingSender(ConnectionResultSender)     │
   → handler.HandleTask                                  │
```

`RunTask` is the current `HandleFrame` body extracted verbatim into a core service reachable via `NodeagentFactoryContext`. The push local scheduler is then ~5 lines. The dispatcher keeps `SendAdvertisement`; nothing else moves.

### D6: Nodeagent schedulers also implement `event::CommandHandler`

Node-internal events (first consumer: the Phase 1 capacity-released signal) are delivered as addressed `Command` messages through the dispatcher's command queue, exactly like `DEFERRED_DELETE` today. Producers (`AdmissionController`, later `DataDepFetcher`) hold only the scheduler's `CommandHandler*` + dispatcher and never depend on `Scheduler`. Two wins: no `Notify*` pollution of the shared interface, and deferral out of the completion/parse stack (`AdmissionScope::~AdmissionScope` fires mid-frame-processing; the command queue drains safely on the loop).

- *Alternative considered*: typed `Notify*` virtuals. Rejected — leaks node-internal events into the cross-side contract and needs an interface edit per new event type.
- *Note*: `Command` is point-to-point (one `destination_`), not broadcast. `StateReporter` keeps polling via its timer; a fan-out handler is a later option if multiple consumers appear.

### D7: Advertisement derives from schedulers

`BuildNodeCapabilities` appends `RequiredProtocol()` for the configured local scheduler(s) to `scheduling_protocols` instead of the hardcoded `"push"`. With the default `push` scheduler this reproduces today's advertisement.

### D8: Config schema

```
GatewayConfig.schedulers : repeated SchedulerConfig
SchedulerConfig          { ExtensionConfig extension; string task_type; }  // empty = default
NodeAgentConfig.scheduler: ExtensionConfig                                  // optional; default "push"
```

The gateway loader SHALL also accept the legacy single `scheduler:` YAML section and normalize it to one default `SchedulerConfig`, so old gateway configs load unmodified.

## Risks / Trade-offs

- **Breaking interface churn** (`Scheduler`, `FactoryContext`, registries) → All confined to Phase 0; the wire is unchanged, so gateway and nodeagent can roll independently. Existing tests are re-wired, not re-specified.
- **Router misconfiguration** (task type matches nothing, no default) → Router delivers an error to the receiver; no silent drops. Startup validates at least one scheduler.
- **`Command` is point-to-point** → `StateReporter` keeps polling; broadcast semantics deferred until a concrete second consumer.
- **Over-reservation is inherent to future probe scheduling** → Not in Phase 0; the Phase 2 design default (queue-then-preallocate-on-release) minimizes it.
- **Test churn** on `nodeagent_tlv_handler_test.cc` → Rewritten against the dispatcher + push local scheduler seam; behavior assertions carry over unchanged.

## Migration Plan

1. **Phase 0A (nodeagent, land first)** — context split + `Scheduler` interface + `RunTask` + push local scheduler + dispatcher + advertisement union. Wire-safe and self-contained; existing nodeagent configs need no changes (push default).
2. **Phase 0B (gateway)** — `GatewaySchedulerFactory`/router + `Schedule` conversion of `round_robin`/`capability_aware` + `GatewayHttpHandler` + loader normalization of legacy `scheduler`.
3. **Rollback** — revert the affected binary; since the wire is byte-identical and config loaders accept both forms, old and new builds interoperate.

## Future phases (designed-for, not built)

The boundary principle this phase plan rests on: **ingress is protocol-owned, execution/egress is core-owned.** A scheduler extension is an *event reactor* — wire frames and node-internal signals in, frames and core-service calls out — not a bag of one-shot policies. Everything below reuses the Phase-0 contracts (`Scheduler` + `HandleFrame` + `RequiredProtocol`, the `CommandHandler` role, `RunTask`, the per-type router, the split contexts). No future phase should need to change the shared `Scheduler` interface.

### Phase 1 — Capacity-released events and queue primitives

**Goal**: give local schedulers a node-internal event source and the data structure deferred admission needs. Establishes the `CommandHandler` role's first real consumer (the role itself is defined in Phase 0).

**Design**:
- `AdmissionController` gains an optional notifier (a `CommandHandler*` destination + the event dispatcher). On `Release()` it submits `{type_: CAPACITY_RELEASED, destination_: scheduler_}`; the dispatcher drains the command queue on the loop, calling `scheduler_->ProcessCommand`.
- Producers never depend on `Scheduler` — they hold only the destination pointer. New internal signals extend the `Command::Type` enum, never the interface.
- The queue drain naturally defers the notification out of the completion/parse stack: `Release()` fires from inside `AdmissionScope::~AdmissionScope` / `AdmissionTrackingSender::Send`, i.e. mid-frame-processing. Directly calling back into the scheduler would recurse on the same stack.
- A bounded queue primitive (deque + max size) owned by the local scheduler. A full queue rejects the incoming probe immediately (see Phase 2).
- **Open decision**: node-global vs per-gateway queues. Recommendation: node-global — it enforces a node-wide probe cap across gateways and avoids one gateway starving another; dequeue fairness (FIFO, priority, per-gateway quota) becomes a scheduler-policy knob. Per-gateway queues would need a cross-queue arbitration policy anyway, since capacity is node-global.
- **Known limitation**: `Command` is point-to-point. `StateReporter` keeps polling via its timer; a fan-out handler is only needed if a second consumer of capacity events appears (e.g. metrics).

### Phase 2 — Probe protocol

**Goal**: Sparrow-style scheduling where the gateway probes a small set of candidate nodes and lets them claim tasks, instead of pushing to a node it believes has capacity. This is the regime that matters at cluster scale, when gateways cannot track cluster-wide resource state.

**Wire and routing**:
- New global TLV ids in `TlvFrame`: `kTaskProbe`, `kTaskProbeCancel` (gateway→node), `kTaskPull` (node→gateway), `kTaskGrant`/`kTaskDecline` (gateway→node). Global allocation keeps both ends coherent by construction (D1/D2 of the earlier TLV discussion: extension-declared ids risk silent mid-protocol drift between independently built endpoints).
- **Implicit routing by type-id is now required, not just convenient**: per-type gateway schedulers mean one gateway↔node connection carries several protocols at once (push for type A, probe for type B). The "activate one protocol per connection" handshake is incompatible with that, so routing stays on `HandledFrameTypes()` and the advertisement's `scheduling_protocols` is a union.
- The gateway's `GatewayTlvHandler` routes `kTaskPull` frames to the probe scheduler's `HandleFrame`; the nodeagent's dispatcher routes `kTaskProbe`/`kTaskProbeCancel`/`kTaskGrant`/`kTaskDecline` to the probe local scheduler.

**Gateway probe scheduler** (`GatewaySchedulerFactory`):
- `Schedule(task, receiver)` selects a fixed small number of candidate nodes (power-of-two sampling over `NodeDirectory::GetCandidates(RequiredProtocol())`), stores the receiver + probe state keyed by `task.id`, and sends `kTaskProbe` (task id + type + `ResourceRequirements`, *not* the body) to each.
- **Race arbiter**: the first `kTaskPull` for a `task.id` wins — the gateway replies `kTaskGrant` carrying the full `Task` (body included) and drops the other candidates' probe state. A `kTaskPull` for an already-granted id gets `kTaskDecline`.
- **Deadline**: a per-task timer cancels outstanding probes (`kTaskProbeCancel`) when no node claims the task in time; the receiver gets an error. Cancels are idempotent.
- Per-task state (probes outstanding, which node, deadline) is scheduler-owned; the receiver stays in `ResultReceiverStorage` until grant/decline/timeout, exactly as today.

**Nodeagent probe local scheduler** (`NodeSchedulerFactory`):
- `kTaskProbe` → enqueue (or admit directly if capacity is available — see the policy knob) and start any data-dependency downloads (Phase 3); a full queue answers with an immediate rejection.
- **Admission policy (configurable)**: *queue-then-preallocate-on-release* (default — the probe is queued, and on `CAPACITY_RELEASED` the scheduler walks the queue, preallocates for the best candidate(s), and pulls) vs *eager-preallocate* (preallocate on probe receipt and pull immediately). Default minimizes the optimistic over-reservation inherent to probing.
- `CAPACITY_RELEASED` arrives via `ProcessCommand` (Phase 1); preallocation calls `AdmissionController::Admit` and, on success, emits `kTaskPull`.
- `kTaskGrant` → `RunTask` (the probe's task id maps to the granted `Task`).
- `kTaskDecline` ("already taken by another node") → `Release()` the preallocation and re-trigger `CAPACITY_RELEASED` so the next queued probe gets a chance.
- `kTaskProbeCancel` → remove from the queue (idempotent; also releases any preallocation).
- **Task readiness predicate**: a probe becomes runnable only when `preallocated ∧ deps cached ∧ grant received`. Preallocated-but-ungranted capacity is a real reservation and needs a timeout (Phase 5).

**Sequence** (queue-then-preallocate variant):

```
gateway                node A                    node B
   │── probe ──────────► queue                   │── probe ──► queue
   │── probe ───────────────────────────────────►│
   │                      [capacity released]    │
   │                      prealloc + pull        │
   │   ◄── pull ──────────┘                      │
   │── grant (full Task) ──►  deps done → RunTask│
   │                      [capacity released]    │
   │                      prealloc + pull        │
   │   ◄── pull ────────────────────────────────►│
   │── decline ─────────────────────────────────►│  release + retrigger
```

**Inherent trade-off**: optimistic over-reservation. Two probed nodes can both preallocate for the same task; only one wins the grant and the loser releases. Capacity accounting must treat held-but-ungranted as a real reservation with a bounded lifetime, and admission policy (queue-first default) keeps the window small.

### Phase 3 — Data dependencies

**Goal**: tasks whose inputs are remote references should start fetching those references while a probe sits in the queue, so the task is runnable the moment it is granted. Task inputs become "values or references resolved to values on demand".

**Design**:
- A `DataRef` proto (`source` + `key`/`sha`) added to `core/node` (or `core/task`); the probe message carries `repeated DataRef deps`. The `Task` input model evolves so bodies/inputs can be inline values or `DataRef`s — designed here because the probe wire format carries them, even though fetching lands this phase.
- A new extension category `DataDependencyFetcher` (`DataDependencyFetcherFactory`, `NodeagentFactoryContext`): pluggable fetch backend (local copy, HTTP, S3, NFS, ...) plus a node-local cache. The scheduler only decides *when to start* a fetch; the fetcher owns the mechanics.
- Completion surfaces as a command (`DEP_COMPLETED`, Phase-1 mechanism) so the scheduler re-evaluates readiness without polling. "Deps already cached" short-circuits instantly.
- Readiness gate (Phase 2) becomes `preallocated ∧ deps cached ∧ grant received`; fetches begin at enqueue time so cache warming overlaps the queue wait.
- **Open decisions**: whether `DataRef` carries explicit source URLs or a type-resolved reference (the latter keeps probes small and enables cache-hint reuse); whether cache eviction is a fetcher concern (recommended: yes, per-node policy).

### Phase 4 — Child tasks and local execution

**Goal**: workflow-style handlers spawn child tasks whose results return to the parent, whether the child runs on this node or is forwarded to a gateway. This activates the nodeagent's `Schedule` facet and makes the node a client as well as a worker.

**Design**:
- `TaskHandler::HandleTask(task, sender)` evolves into an invocation context bundling the sender + a submission handle (the main interface churn in the pipeline; deferred to this phase so 0–3 don't touch existing handlers).
- The parent registers a *local receiver* keyed by the child's `task.id`, then calls `Schedule(child, receiver)` — the exact contract a gateway HTTP client uses. Local vs remote is indistinguishable to the parent.
- **Local run**: if capacity allows, the local scheduler routes the child straight into `RunTask` with a receiver-backed sender.
- **Forward**: otherwise it submits the child to a gateway via an egress `GatewayClient`. Preferred routing: reuse a live gateway connection with an upstream submission frame (the gateway treats it as a normal task through its router), falling back to an outbound connection when none is live.
- **Two-hop result routing for forwarded children**: gateway → result frame → nodeagent connection → the node's local receiver registry (a `task.id → parent receiver` map, the nodeagent mirror of `ResultReceiverStorage`) → parent. The nodeagent's inbound handler recognizes child-result ids and delivers locally instead of treating them as submissions.
- **Decision ownership**: local-run-or-forward belongs to the nodeagent local scheduler — it is the node's capacity authority. The child path is deliberately *not* probe-based: the parent is local, so the probe dance is pointless; the policy is "run immediately if capacity, else forward", and the gateway then schedules the forwarded child with its normal per-type router.
- The nodeagent local receiver registry and `GatewayClient` both enter `NodeagentFactoryContext` as this phase lands.

### Phase 5 — Hardening

- Deadlines/TTLs: probe TTL, preallocated-but-ungranted timeout, task TTL (ties into the per-task state already carried by the probe scheduler).
- Idempotency of `cancel`/`grant`/`decline` (a late cancel after grant is a no-op; a repeated decline releases once).
- Queue fairness across gateways and probe-admission policy tuning.
- Metrics: probe queue depth, preallocation-wait, grant/decline rates, over-reservation statistics.

## Open Questions

- **`SchedulerConfig` naming**: `extension` vs flat `name`/`typed_config` fields inside `SchedulerConfig` — the former nests cleanly, the latter matches existing YAML style. Leaning `extension`.
- **Legacy gateway config**: confirm the loader should silently normalize `scheduler:` → `schedulers:` rather than fail on the old shape (recommended: normalize).
- **`FunctionResolver` removal from the shared context** touches `NodeDiscoveryFactory` and any test mocks that construct `FactoryContextImpl` — confirm this churn is acceptable in Phase 0B (it is a natural consequence of D2).
