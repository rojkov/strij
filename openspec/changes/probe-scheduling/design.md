## Context

v1 schedules by push: the gateway chooses a node (`round_robin` rotation or `capability_aware` least-loaded) and writes a `kTaskSubmission` carrying the full `Task`. Node selection relies on state the gateway tracks (`ExactStateTracker` snapshots), which does not scale — a gateway cannot know cluster-wide resource state, and O(1) push into a contended node just moves rejection to the node.

Phases 0–1 built the mechanism this change consumes:
- The shared `Scheduler` contract (`Schedule`, `RequiredProtocol`, `HandleFrame`, `HandledFrameTypes`) with per-type router on the gateway and a frame dispatcher (`NodeagentTlvHandler`) on the node.
- The `CAPACITY_RELEASED` event (`AdmissionController::RegisterCapacityObserver`, pure-wakeup `Command` with `args_ == nullptr`) that fires on every successful admission release.
- The `BoundedQueue<T>` primitive and a `RunTask` service that admits and runs via the configured handler.

Nothing yet *uses* the wakeup or the queue. This change makes the node's own live capacity the scheduling authority.

## Goals / Non-Goals

**Goals:**
- A probe protocol where the gateway samples a small candidate set and lets nodes claim tasks against live capacity.
- Node-side deferred admission: bounded queue, preallocation on release, self-resolving reservation lifecycle.
- Gateway-side race arbitration (first pull wins), prompt reclamation of losers, deadline safety, and the "every task resolves" invariant.
- Zero behavior change to the existing `push` / `round_robin` / `capability_aware` paths.

**Non-Goals:**
- Data-dependency fetching (Phase 3), child tasks / node-originated scheduling (Phase 4), preallocation TTLs and fairness knobs (Phase 5). The readiness predicate here is `preallocated ∧ grant received`; `deps cached` arrives with Phase 3.
- A new one-shot timer primitive. The Phase 2 deadline uses the existing `PeriodicTimer` sweep; an earliest-deadline timer is a Phase 5 refinement.
- Any change to the shared `Scheduler` interface or the routing seams.

## Decisions

### D1: Wire frames — five new TLV ids, decline is node→gateway

```
direction    type              payload                   semantics
─────────────────────────────────────────────────────────────────────────────
gw → node    kTaskProbe        TaskProbe{id,type,reqs}   "here is a task; claim it"
gw → node    kTaskGrant        Task (full body)          "you won; run this" (one node only)
gw → node    kTaskProbeCancel  TaskProbeCancel{id}       "relinquish" (revocation)
node → gw    kTaskPull         TaskPull{id}              "I hold capacity; grant me"
node → gw    kTaskDecline      TaskDecline{id,reason}    "I refuse" (pre-commit refusal)
```

Key correction to the Phase 0 sketch, which put `kTaskDecline` gateway→node:
- There is no node→gateway refusal under that scheme. `kTaskRejected` is push-path-shaped — `GatewayTlvHandler` looks the id up in `ResultReceiverStorage` (`gateway_tlv_handler.cc:105`), and under D6 probe receivers are *not* in storage yet, so a rejected probe would fall on the floor ("No receiver", frame dropped) and the task would hang until the deadline.
- `kTaskDecline` therefore means *node refuses a probe* and is the probe scheduler's own protocol frame, resolved against its own probe-state map. `kTaskProbeCancel` is the gateway's *revocation* (losers at grant time, deadline expiry, late/duplicate pulls). Pre-commit refusal (decline) and post-commit revocation (cancel) are distinct intents in distinct directions.
- **Allocation** (appended, nothing renumbered): `kTaskProbe=6`, `kTaskProbeCancel=7`, `kTaskPull=8`, `kTaskGrant=9`, `kTaskDecline=10`. Existing type ids 0–5 are untouched. Old endpoints drop unknown ids (`GatewayTlvHandler` default branch, `NodeagentTlvHandler`), so the addition is wire-compatible for existing traffic.

*Alternative considered*: a dedicated `kTaskProbeRejected` frame. Rejected — `kTaskDecline` already expresses "refusal"; a separate frame would need the same gateway-side handling and add an id without new semantics.

### D2: Probe protobuf messages in `api/common/task/probe.proto`

`TaskProbe{id, type, ResourceRequirements requirements}`, `TaskPull{id}`, `TaskProbeCancel{id}`, `TaskDecline{id, reason}`. The grant carries a serialized `Task` — the full body is sent only to the winner; probes never carry the body (task bodies are usually large; probe volume is 2^k per task). `requirements` is the authoritative field (see the carried-requirements change), so the node can `Admit` against the probe alone.

### D3: Node admission decision tree

On `kTaskProbe{id,type,reqs}`:

```
    walk();                     // drain backlog: oldest first, Admit what fits
        │
        ├─ Admit → ok ─────────► prealloc a slot; send kTaskPull{id}; state=pull-pending
        ├─ ResourceExhausted ──► transient → Push into BoundedQueue
        │                          room?  → state=queued (drained by CAPACITY_RELEASED)
        │                          full   → kTaskDecline{reason:"queue full"}
        └─ FailedPrecondition ─► never satisfiable (undeclared pool / reqs > pool total)
                                   → kTaskDecline{reason:"requirements unsatisfiable"}
```

`Admit` already returns exactly these failure classes (`admission_controller.cc:36,41,50`), so the enqueue-vs-decline split falls out of existing semantics. The `walk()` on arrival is a cheap no-op in steady state (capacity was exhausted by the previous arrival) but preserves FIFO precedence when a barging arrival coincides with freed capacity.

- **Full queue → decline regardless of current capacity.** The bounded queue is a cap on *unfilled reservations*, not a scheduler queue: active preallocations are already bounded by pool totals, so the queue bounds the waiting backlog. Strictness on the full-check keeps the cap meaningful.
- **Decline is fire-once-idempotent**, keyed by task id. If the queue frees before a decline lands, the node simply never serves that id — overdecline is harmless (task resolves via another candidate or deadline).

*Alternative considered*: "no response + gateway deadline". The node staying silent pushes a full node's rejection into the deadline window (seconds of latency), exactly the pathology probe scheduling exists to avoid. A one-round-trip decline resolves the task (or moves on to the next candidate) immediately.

### D4: Node-side reservation lifecycle and the `RunTask` seam

```
queued ──walk/release──► pull-pending ──grant──► running ──result──► done
   │                        │
   └──── cancel ────────────┴──── cancel/decline   (Release() → broadcast → walk)
```

The probe scheduler registers as a capacity observer at construction (`AdmissionController::RegisterCapacityObserver(this)`), so every release — task completion, decline, cancel — wakes it and drains the queue automatically. This is D7 of the Phase 1 design paying off: no explicit "re-trigger" code path.

**The seam change this phase forces:** the preallocation is a *held* `AdmissionController::Admit`. When grant arrives, the task must run *without* calling `Admit` a second time (a double-admit would never unwind). The public `RunTaskService` (`include/strij/nodeagent/run_task_service.hh`) gains a variant:

```
virtual void RunTask(task, conn,
                     nodeagent::AdmissionScopePtr reserved) PURE;   // "reserved" skips Admit
```

The scheduler hands its held scope to the service, which wraps the handler with `AdmissionTrackingSender(ConnectionResultSender(conn.Mailbox()), std::move(reserved))` exactly as `RunTask` does today (`run_task_service.cc:52-55`). On final result the scope releases → `CAPACITY_RELEASED` → next queued probe walks. The current `RunTask` is unchanged for the push path.

*Alternative considered*: the scheduler reaching `TaskHandlerManager` itself. Rejected — it couples an extension to core internals; the service is the sanctioned boundary. The scope-carrying variant keeps the execution path central.

### D5: Admission policy — one default, overbooking as a number

The Phase 0 sketch's "queue-then-preallocate-on-release vs eager-preallocate" knob is under-specified: read literally, queue-first would starve a probe that arrives with capacity free and never sees a release. Both variants must admit-at-arrival when capacity is free to avoid that. Phase 2 ships **one policy**:

- probe arrival → `walk()` then admit-or-enqueue (D3);
- `CAPACITY_RELEASED` → `walk()`;

with a configurable queue capacity and a `max_concurrent_preallocations` cap that bounds how many queued probes the walk preallocates in one pass (the overbooking dial). A single sane default keeps v1 simple; fairness/priority/preallocation-TTL knobs are Phase 5.

### D6: Receiver lifetime — probe map, then storage at grant

`ResultReceiverStorage::Put(task_id, receiver, node_id)` binds a receiver to the *one* node that will run it (for `NotifyNodeDisconnected` cleanup). During probing there is no single node, so:

- `Schedule()` stores the receiver in the probe scheduler's own per-task state (with the full `Task` and probed set). **Not** in storage.
- At grant: `storage.Put(id, receiver, winner_node_id)` and the probe state is erased. `kResult` frames deliver via the existing path (`gateway_tlv_handler.cc:120`).
- On all-declined / deadline: `receiver->DeliverError(...)` from the probe map, erase — storage is never touched.

*Alternative considered*: `Put` at `Schedule` time with a placeholder node id. Rejected — `NotifyNodeDisconnected` would clean up probing tasks still waiting on *other* nodes (wrong outcomes), and `kResult` lookups would hit the storage map for ids that haven't been granted yet.

**Invariant:** every task handed to `Schedule` resolves exactly once — a `kResult` via storage, or a `DeliverError` from the probe map (all-declined, no-eligible, deadline). This preserves the `Scheduler` contract's no-hang rule.

### D7: Gateway arbitration — first pull wins, cancel the rest

```
Schedule(task):
  k = min(candidate_count, candidate set size)   // clamp; task.id-seeded sample, no replacement
  none → DeliverError
  send kTaskProbe to each; state{task, receiver, probed, deadline} keyed by task.id

kTaskPull{id} from conn:
  state? grant → kTaskDecline→... no: state is GRANTED/erased → kTaskProbeCancel{id}  [revoke]
  probing, first → win:
      kTaskGrant{full Task} on conn
      kTaskProbeCancel{id} to the OTHER probed nodes   [reclaim their capacity NOW]
      Move receiver+state: storage.Put(id, receiver, winner); erase probe state

kTaskDecline{id} from a probed node:
  state probing → drop that candidate
      all-declined → DeliverError(reason); erase       [immediate, no deadline wait]
      else → keep waiting (pull or deadline)

deadline sweep (PeriodicTimer tick):
  expired probes → kTaskProbeCancel to all probed; DeliverError("probe deadline"); erase
```

Cancel-at-grant is the **over-reservation valve**: loser nodes may already hold preallocations; canceling them returns that capacity immediately (each loser release re-triggers its own queue walk). The archive's stated goal —"admission policy keeps the window small" — is enforced by the gateway, not the node.

**Candidate sampling.** Default `candidate_count = 2`: the power-of-two-choices benefit is concentrated in the 1→2 step (Sparrow ships d=2; gains beyond ~3 are marginal), and it keeps the over-reservation window narrow — every probed node that admit-and-pulls holds a real reservation, so k multiplies the losers waiting for grant-time cancellation. The effective sample clamps to the candidate set: a one-node probe-capable cluster degrades to k=1 (probe exactly one node; its decline or pull decides immediately), and an empty set delivers an error. The sample is drawn **without replacement, seeded by `task.id`** (per-`(task.id, node)` hash/PRNG) so successive tasks don't probe the same contended pair while the draw stays reproducible for tests. Multi-gateway pressure on a node's shared queue is acknowledged and left for Phase 5 (per-gateway quota), not "fixed" here.

**Per node per task invariant:** the gateway emits *at most one* of `{grant, cancel}`. Cancel only ever goes to non-winners; a grant and cancel for the same `(node, task)` cannot coexist by construction. A node receiving cancel while it believes the task granted is a protocol bug → log-and-drop (never double-run).

### D8: Deadline mechanism — periodic sweep for Phase 2

The framework has only `PeriodicTimer` (fixed-interval timerfd + read). The probe scheduler owns a single `PeriodicTimer` (e.g. 100 ms cadence) and a probe-state map; each tick expires due deadlines (destroy → `DeliverError` + cancel to remaining probed nodes). Cost is O(probes-in-flight) per tick; the probe window is short-lived so steady-state map size is small. A min-heap + re-armable earliest-deadline timer is a Phase 5 refinement (also needed for the preallocation TTL), not Phase 2 scope. The deadline and `candidate_count` co-design: a healthy node returns a pull in ~1 RTT, so the default `probe_deadline` is a small multiple of expected RTT (1–2 s), configurable; the all-declined fast-path keeps operator-set deadlines from having to be tight.

*Alternative considered*: per-task one-shot timerfds. Rejected — hundreds of probe tasks would mean hundreds of timerfds and CQE slots; one sweeping timer amortizes to zero framework churn.

### D9: Node connection retention via `OutboundMailbox`

Queued/pull-pending probes retain the originating connection's `OutboundMailbox` (shared_ptr), the same pattern `StateReporter` uses (`state_reporter.hh:37`). The walk (triggered by a `CAPACITY_RELEASED` command drained later) writes `kTaskPull` on that mailbox even if the connection has since changed state; writes to dead mailboxes are no-ops. `kTaskGrant`/`kTaskDecline`/`kTaskProbeCancel` arrive on whatever live connection carried them and are resolved by task id; grant's result channel is the frame's own connection.

### D10: Config shape

```
nodeagent.schedulers[] probe:    ExtensionConfig{ name:"probe",
                                   typed_config: ProbeSchedulerConfig{ queue_capacity,
                                                                       max_concurrent_preallocations } }
gateway.schedulers[] probe:      SchedulerConfig{ task_type:"" (default or per-type),
                                   extension:{ name:"probe",
                                               typed_config: ProbeRoleSchedulerConfig{ candidate_count: 2,
                                                                                      probe_deadline: 1s } } }
```

Defaults: `queue_capacity` and `max_concurrent_preallocations` are operator-tuned (no global default beyond "a queue exists"); gateway side defaults to `candidate_count = 2` and `probe_deadline = 1s`. Startup validation rejects `candidate_count < 1`; the deadline is clipped at the scheduler's tick granularity with no lower bound beyond that. Named `ProbeSchedulerConfig` on each side (`api/nodeagent/extensions/schedulers/probe/`, `api/gateway/extensions/schedulers/probe/`), registered via the existing factories. `BuildNodeCapabilities` picks up `"probe"` in `scheduling_protocols` automatically from `RequiredProtocol()` (Phase 0's D7). Both halves declare the same protocol name `"probe"`; the gateway router's per-type binding decides which task types route through probing.

## Sequence (happy path + losers)

```
gateway                          node A                     node B
  │── kTaskProbe ────────────────► queue                    │── kTaskProbe ──► queue
  │                                  Admit ok → pull        │                    Admit ok or enqueue
  │   ◄──── kTaskPull{id} ──────────┤                       │
  │ FIRST pull: grant A, cancel B   │                       │
  │── kTaskGrant{Task} ────────────►│ scope → RunTaskScope   │
  │── kTaskProbeCancel ────────────►│ (already errored alt)  │── kTaskProbeCancel ──► release prealloc
  │   ◄──────── kResult ────────────┤                       │        [release → broadcast → walk]
  │ (storage → HTTP client)         │                       │
```

```
gateway                             full node (both)          outcomes
  │── kTaskProbe ──────────────────► │  queue full → decline
  │   ◄──── kTaskDecline{full} ──────┤
  │   ◄──── kTaskDecline{full} ──────┤
  │ ALL declined → DeliverError now  │
```

## Risks / Trade-offs

- **[Over-reservation]** Two probed nodes can both preallocate the same task; the loser releases on cancel/decline. The window is bounded by `max_concurrent_preallocations` and closed by grant-time cancellation. → Built-in; the release-broadcast makes reclamation automatic.
- **[Preallocated-but-ungranted reservation leak]** If a gateway dies with probes in flight, nodes hold preallocations with no deadline (the node-side preallocation TTL is Phase 5). → Acknowledged; bounded by `max_concurrent_preallocations`; landed as a Phase 5 hardened timeout.
- **[Deadline sweep cost]** O(n) per tick over the probing set. → Probe window is short-lived; refine to an earliest-deadline timer in Phase 5 at scale.
- **[Correlated probe pairs]** A plain `take-k` from `GetCandidates()` can keep probing the same contended nodes task after task. → Task-id-seeded sampling decorrelates successive probes; k=2 already halves probe load vs larger k.
- **[Duplicate-pull race]** Two pulls processed in the same batch → first (in CQE order) wins; the second gets a cancel. Deterministic per event-loop thread. → No special handling; covered by the cancel path.
- **[Starvation of queued tasks needed but declined task]** A task enqueued with requirements that fit no pool gets a `FailedPrecondition` decline at arrival, never entering the queue. → The decision tree prevents "queue corpses".
- **[Wire addition rolling out over a mixed fleet]** Old endpoints drop probe frames silently. → Additive ids; probe config is opt-in per side; mixed rollout degrades to push-and-probe-on-new-endpoints, never a crash.

## Migration Plan

1. **Node side first**: TLV ids + `probe.proto` + `RunTaskService` scope variant + node probe scheduler (queue/walk/pull/decline) + tests — verifiable standalone against unit tests; no production node runs a probe scheduler until configured.
2. **Gateway side**: gateway probe scheduler (sampling/arbiter/cancel/deadline) + tests. Both halves must be configured together by an operator for the protocol to be left on; neither half changes existing push behavior.
3. **Rollback**: remove the scheduler entries from config on both ends; existing binaries contain the code but never activate it. Old endpoints ignore the new frame ids. No renumbering, no protocol break to revert.

## Open Questions

None blocking. Phase 5 retains: preallocation TTL, earliest-deadline timer, multi-gateway queue fairness (per-gateway quota), candidate-set tuning (`candidate_count` vs task arrival rate), and metrics (probe queue depth, grant/decline rates, over-reservation stats).