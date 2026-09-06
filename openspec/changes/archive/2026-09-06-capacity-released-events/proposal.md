# Capacity-released events (Phase 1 of pluggable task scheduling)

## Why

`AdmissionController::Release()` is silent today: capacity that frees up when a task completes is simply subtracted from the accounting maps, and nothing tells the local schedulers. The Phase 0 design (archived `2026-08-30-pluggable-task-scheduling`) reserved the nodeagent scheduler's `event::CommandHandler` role for a "capacity-released signal" — its first real consumer. Deferred admission (the Phase 2 probe scheduler) needs to wake when capacity frees: a task completion must trigger the scheduler to re-examine its queue and preallocate. This change builds that wakeup mechanism and the bounded-queue primitive deferred admission will sit on, before the probe protocol consumes it.

## What Changes

- **`Command::Type` gains `CAPACITY_RELEASED`** in `include/strij/event/command.hh`. The signal is a *pure wakeup*: `args_` is `nullptr` and the receiving scheduler re-queries the `AdmissionController` for current capacity. No aggregated payload, no `args_` lifetime management on the hot release path.
- **`AdmissionController` becomes a broadcaster.** The public interface gains `RegisterCapacityObserver(event::CommandHandler*)`; the impl holds the observer list plus a `Dispatcher&` (constructor-injected) and, on each successful `Release()`, submits a `CAPACITY_RELEASED` command to every registered observer. **Node-global broadcast**: the controller is shared by all local schedulers, and freed capacity is a node-global fact, so every registered scheduler is notified; each re-checks admission itself.
- **No scheduler registers in this phase.** The `"push"` local scheduler keeps a no-op `ProcessCommand` and does not register — waking it would be a wasted command per release. The observer list is empty in production, so **runtime behavior is byte-for-byte unchanged**. The wiring is latent until the Phase 2 probe scheduler self-registers in its factory `Create()`.
- **A reusable `BoundedQueue<T>` primitive** (deque + max size) lands in `strij::utils` (`src/common/core/utils/`). Full queue rejects `Push()` with `false`. Single-threaded by design (runs on the event-loop thread), no locking. It ships tested on its own; the probe scheduler owns an instance in Phase 2.
- **`AdmissionControllerImpl` constructor gains a `Dispatcher&`** — constructor churn in `nodeagent_framework.cc` and the admission controller tests; no wire or config change.

## Capabilities

### New Capabilities
- `bounded-queue`: the reusable `BoundedQueue<T>` primitive — constructor max-size bound, full-queue rejection, standard accessors, single-threaded ownership contract, and the "reject incoming probe immediately when full" semantics it will serve.

### Modified Capabilities
- `node-local-scheduler`: the unexercised `CommandHandler` role gains its first wiring — nodeagent local schedulers can register as capacity-released observers with the shared `AdmissionController`, which notifies them on capacity release; the push scheduler explicitly does not register (behavior preserved).
- `command-handler`: `Command::Type` gains `CAPACITY_RELEASED`, delivered as a `args_ == nullptr` pure-wakeup command; the receiving `CommandHandler` re-queries its sources of truth rather than reading a payload.

## Impact

- **Code**: `include/strij/event/command.hh` (enum), `include/strij/nodeagent/admission_controller.hh` + `src/nodeagent/core/admission_controller.{hh,cc}` (observer API + broadcast), `src/nodeagent/exe/nodeagent_framework.cc` (constructor), new `src/common/core/utils/bounded_queue.hh`.
- **Tests**: `admission_controller_test.cc` (release-broadcasts scenarios via `MockDispatcher`), new `bounded_queue_test.cc`; existing push/TLV-parse tests unchanged.
- **No wire change, no config change, no behavior change** in the running system: the observer list is empty with only `push` configured.