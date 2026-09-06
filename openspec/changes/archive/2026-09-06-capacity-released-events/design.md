# Design: Capacity-released events (Phase 1 of pluggable task scheduling)

## Context

The archived `2026-08-30-pluggable-task-scheduling` change (Phase 0) established the mechanism for pluggable scheduling but left the nodeagent's `event::CommandHandler` role unexercised. In the current code:

- `AdmissionController` (a `shared_ptr` shared across all local schedulers via `NodeagentFactoryContext`) enforces admission in `Admit()` and releases capacity in `Release()`, which is **silent** — it decrements counters and returns.
- Both release paths funnel into the same controller chokepoint: `AdmissionScope::~AdmissionScope` (task dropped, RAII) and `AdmissionTrackingSender::Send` (normal final result). This is deliberate: the notification point is structurally forced to coincide with the single capacity-freeing point.
- `PushLocalScheduler` already implements `event::CommandHandler` (`ProcessCommand` is a no-op) per the Phase 0 contract, so the destination seam exists on every local scheduler.
- `Command::Type` is `{ACTIVATE_READ, DEFERRED_DELETE}`; `Command.destination_` is `CommandHandler*`, `args_` is `void*`. Commands queue in the dispatcher and drain at the top of each `Run()` loop iteration.

Phase 1 (this change) is the first real consumer of the `CommandHandler` role: give local schedulers a node-internal capacity-released event source and ship the bounded-queue primitive deferred admission (Phase 2) will sit on. The probe scheduler itself is out of scope.

## Goals / Non-Goals

**Goals:**
- A node-internal `CAPACITY_RELEASED` signal that wakes each registered local scheduler on the event loop, deferred out of the completion/parse stack.
- The notification is a **pure wakeup**: no payload, no `args_` allocation, no lifetime management. The receiver re-queries the controller.
- **Node-global broadcast**: `AdmissionController` notifies every registered observer, because freed capacity is a node-global fact and the scheduler is shared.
- A reusable, tested `BoundedQueue<T>` primitive (deque + max size) in `strij::utils`.
- **Zero behavioral change** in the running system while `"push"` is the only scheduler: the observer list is empty, so releases submit no commands.

**Non-Goals:**
- The probe protocol, preallocation, pull/grant races (Phase 2).
- Queue-fairness policy knobs (FIFO, priority, per-gateway quota) — later, when the queue has a consumer.
- Pool-filtered observers ("only wake me when pool `gpu` frees") — Phase 2 tuning, when spurious wakeups have a real cost.
- A data payload on the wakeup ("what exactly freed") — rejected, see D3.
- The queue primitive's first consumer — the probe scheduler (Phase 2).

## Decisions

### D1: `CAPACITY_RELEASED` as a pure-wakeup command, `args_ == nullptr`

```
        │  Release() — task completes / scope dies
        ▼
   AdmissionController::Release(task_type, requirements)
        │  decrement pool_in_use / type_in_flight (existing logic)
        ▼
   for each observer in observer_list_:
        ▼
   dispatcher.SubmitCommand({
        .type_        = Command::CAPACITY_RELEASED,
        .destination_ = observer,      // a local scheduler
        .args_        = nullptr,       // pure wakeup — no payload
   })
```

The receiving scheduler, inside `ProcessCommand`, does **not** read `args_`. It re-queries `AdmissionController::SharedFree()` / `InFlight()` (and, in Phase 2, its own queue) and decides. The controller remains the source of truth for capacity.

- **Why pure wakeup over a payload?** `Command.args_` is a raw `void*` with the `DEFERRED_DELETE` ownership convention (submitter's `this`, destination frees). A capacity payload has no natural owned object; heap-allocating one per `Release()` on the hot path (a release fires twice per task: `AdmissionTrackingSender::Send` *and* `AdmissionScope::~AdmissionScope` — though the scope is idempotent-guarded) would force lifetime management in every consumer. A wakeup forces each consumer to re-derive state from the controller, which is what the queue-walking scheduler wants anyway.
- **Spurious wakeups are harmless by construction**: the receiver re-verifies admission before acting, and a release of unrelated pool capacity (e.g. `cpu` freed while the queue waits on `gpu`) is an idempotent no-op.

### D2: Node-global broadcast — observer list, not a single destination

The design sketched a single `CommandHandler*` notifier. Rejected: `AdmissionController` is a shared singleton across all local schedulers, and freed capacity is node-global — a release caused by a `push`-scheduled task must also wake the probe scheduler (Phase 2) waiting on its queue. With a single destination, only one scheduler knew; with a list, every registered scheduler is woken and re-checks.

```
   RegisterCapacityObserver(CommandHandler*)      // public interface
   RegisterCapacityObserver(CommandHandler*)      // no unregister: schedulers are
                                                  // process-lifetime singletons
```

Played forward, this is the *whole* point of Phase 2's evolution: push and probe coexist on one node, share one admission controller, and both learn when the shared capacity changes. Registration happens once, in the scheduler factory's `Create()`. Schedulers (and the controller) are built at startup and never torn down until process exit, so there is no unregistration and no dangling-observer window in production (tests tear down in the opposite order: dispatchers/mocks die before the controller, or use scoping that outlives the release).

### D3: Registration on the *public* `AdmissionController` interface

Local schedulers are extensions; they reach admission via `NodeagentFactoryContext::AdmissionController()`, which returns the *interface* (`AdmissionControllerSharedPtr`). If registration lived only on `AdmissionControllerImpl`, the Phase-2 probe scheduler (which only ever sees the interface) could not register. So `RegisterCapacityObserver(event::CommandHandler*)` lands on the public interface in `include/strij/nodeagent/admission_controller.hh`. This couples the nodeagent admission header to `strij/event/command_handler.hh` — both live under `include/`, so the include-purity check holds.

### D4: `AdmissionControllerImpl` gains a constructor-injected `Dispatcher&`

The controller must submit commands, so it needs the dispatcher:

```cpp
AdmissionControllerImpl(const node::NodeCapabilities& capabilities,
                        event::Dispatcher& dispatcher);
```

Constructor injection (vs. a later setter) keeps the observer wiring impossible to forget. Churn: `nodeagent_framework.cc` (one call site) and the admission controller tests (several `make_shared` sites). The `Dispatcher&` is non-owning; the dispatcher outlives the controller (startup order).

### D5: Push scheduler does not register — latent wiring, zero behavior change

`PushLocalScheduler::ProcessCommand` stays a no-op and the `"push"` factory does **not** call `RegisterCapacityObserver`. Registering it would submit a wasted command (and a no-op wake) on every release. The Phase-1 invariant is therefore observable and testable: with `push` configured, `Release()` broadcasts to an empty list, so **no** commands are submitted and the running system behaves byte-for-byte as before. The mechanism is proven by unit tests against a `MockDispatcher` + a mock `CommandHandler`, not by production wiring.

### D6: `BoundedQueue<T>` — reusable primitive in `strij::utils`

```cpp
template <typename T>
class BoundedQueue {
  // Push(item) -> bool        // false when Full(); no item is consumed
  // TryPop() -> std::optional<T>
  // Peek() -> const T*        // front without removal
  // Size(), Empty(), Full(), MaxSize(), Clear()
};
```

- **Deque + max size.** A full queue rejects `Push()` with `false` — the Phase 2 probe path ("full queue rejects the incoming probe immediately") maps directly onto this.
- **Single-threaded by contract.** The queue is owned by a scheduler running on the event-loop thread; no locking. Documented, not enforced (no mutex), matching the `AdmissionController`'s own "single event-loop thread, no locking" precedent.
- **No probe-specific semantics** (readiness predicates, per-gateway fairness) — those are scheduler-policy concerns that arrive with the consumer in Phase 2. The primitive stays generic and testable on its own.

### D7: Notification timing relative to accounting

`Release()` notifies **after** the counter decrements, and only when the release is not an underflow no-op. The contract to observers is "capacity may have changed; re-check." Where the existing underflow paths `continue` (per-pool) there may be partial freeing; the loop still >= some observers were released, so the broadcast fires for any non-fully-underflowed release. The idempotent re-check makes underflow edge cases harmless.

## Sequence

```
   Task completes normally:                      Task dropped mid-flight:
   ───────────────────────                      ───────────────────────
   AdmissionTrackingSender::Send(result)        AdmissionScope::~AdmissionScope
        │  is_final ──► scope_->Release()            │
           │                                        │
           └───────────────┬────────────────────────┘
                           ▼
        AdmissionController::Release(task_type, requirements)
                           │  decrement counters (existing logic)
                           ▼
        [observer_list_ empty → returns; nothing submitted]
            │  (Phase 2: for each registered observer
            │   dispatcher.SubmitCommand(CAPACITY_RELEASED, observer, nullptr))
            ▼
        Dispatcher drains command queue at top of next Run() iteration
                            │   (deferred out of completion/parse stack)
            ▼
        scheduler.ProcessCommand(CAPACITY_RELEASED)
            │  (Phase 2: re-query SharedFree()/queue; preallocate + pull)
```

The deferral property the Phase 0 design demanded is preserved: `Release()` fires mid-frame-processing (inside `AdmissionTrackingSender::Send`, which runs under the connection's read completion), and the notification is *queued*, never delivered inline. The probe scheduler's Phase 2 reaction (walk queue → admit → pull) therefore never runs on the completion stack.

## Risks / Trade-offs

- **Broadcast fan-out cost on every release** → The list is empty in Phase 1 (free); with a handful of observers it is a loop of `SubmitCommand` calls, each a vector push. Submit of a `nullptr`-args command is cheap; the dominant cost is the receiver's queue re-scan, which is exactly what deferred admission needs to do anyway.
- **A `void* args_` that lures future payloads** → The pure-wakeup contract is documented in the spec (`args_ == nullptr` for `CAPACITY_RELEASED`); a later phase that needs data must add a new command type, not overload this one.
- **Public interface exposes `event::CommandHandler` in the nodeagent admission header** → Same include-boundary family as the rest of `include/`; the header-purity check (`make check_includes`) passes since both are under `include/`.
- **Dangling observers if scheduler/controller teardown order ever diverges** → Both are startup-built, process-lifetime objects; no unregistration path exists. Tests manage lifetimes so the controller (or mock) outlives mock observers.
- **Underflow releases could spuriously wake** → D7 keeps the notify-when-released rule simple; receivers re-check admission idempotently, so a wrong wake is a no-op.

## Migration Plan

1. Land the `BoundedQueue` util + tests first (pure additive, `strij::utils`).
2. Add `CAPACITY_RELEASED` to `Command::Type`.
3. Extend `AdmissionController` interface + impl with the observer list and the broadcast; rewire the constructor (`nodeagent_framework.cc`).
4. Repoint admission tests to the new constructor and add broadcast scenarios against a `MockDispatcher` + mock `CommandHandler`.
5. **Rollback**: revert the `AdmissionController` change — no wire, config, or behavior change shipped, so old/new binaries interoperate trivially. Nothing in the running path depends on the new code until a scheduler registers (none do).

## Open Questions

None blocking. Two deferred by decision, noted for Phase 2:
- Pool-filtered observer registration ("wake me only when pool `X` frees") — revisit if spurious wakeups prove hot.
- `BoundedQueue` eviction/fairness policy — belongs to the scheduler that owns an instance.