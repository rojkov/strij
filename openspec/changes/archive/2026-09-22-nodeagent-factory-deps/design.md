# Design

## Context

See `proposal.md` — Why. The relevant current state:

- `NodeagentFactoryContext` is a single abstract interface (`include/strij/extensions/factory_context.hh`) with nine accessors. Its concrete impl (`src/nodeagent/exe/nodeagent_factory_context.{hh,cc}`) holds three raw pointers (`run_task_service_`, `data_dependency_fetcher_router_`, `child_task_submitter_`) that are null until `Set*()` runs.
- `RunNodeagent` (`src/nodeagent/exe/nodeagent_framework.cc`) builds the handler manager, then the fetcher router, then the scheduler router, then patches the context — in an order that lets a handler factory read the submitter before it is installed.
- The service graph has one cycle: `TaskHandlerManager` → (workflow) `ChildTaskSubmitter` → `NodeagentSchedulerRouter` → scheduler → `RunTaskService` → `TaskHandlerManager`.
- The gateway side already uses a narrow, immutable context (`GatewayFactoryContext` with `NodeDirectory` + `ResultReceiverStorage` + dispatcher) and is not part of this change.

## Goals / Non-Goals

**Goals:**

- Eliminate `NodeagentFactoryContext` and its `Set*()` methods.
- Give each nodeagent extension category only the services it uses.
- Break the construction cycle by ordering, with no late-bound indirection.
- Make a handler factory's child submitter valid by construction (fix the latent workflow null-submitter bug).
- Keep the gateway side untouched.

**Non-Goals:**

- Changing the `Scheduler`, `TaskHandler`, `ResultSender`, `ChildTaskSubmitter`, or `ChildTaskForwarder` behavioral contracts.
- Abstracting `DataDependencyFetcherRouter` behind a pure-abstract interface (a pre-existing `src/` leak; tracked separately).
- Splitting the gateway `GatewayFactoryContext`.
- Changing runtime behavior or wire protocol.

## Decisions

### D1: Per-category reference bundles, not positional parameters, not one context

Each nodeagent factory interface gets a `*Deps` struct of references (plus `AdmissionControllerSharedPtr` by value), defined beside the factory. `Create` takes it by const reference.

- **Why not positional parameters:** the registry forces one `Create` signature per factory category, so all implementations would carry the union anyway; positional args also break source compatibility for extension authors on every service addition and invite transposition.
- **Why not one context:** it is the source of the knot — it is both an input to factory construction and an output of it, so it can only be populated incrementally.
- **Why references:** reference members cannot be default-constructed and aggregate initialization must name every field, so a missing service is a compile error rather than a runtime null. This is the property that makes the workflow bug unwritable.

### D2: Build order — empty manager, then services, then handlers

```
RunNodeagent
  │
  ├─ dispatcher, function_resolver, object_cache, gateway_client, capabilities, admission
  │
  ├─ TaskHandlerManager (empty, shared_ptr)
  ├─ RunTaskServiceImpl(task_handler_manager, admission)
  │
  ├─ DataDependencyFetcherDeps ──▶ fetchers ──▶ DataDependencyFetcherRouter::Build
  │
  ├─ NodeSchedulerDeps ──▶ NodeagentSchedulerRouter   (== ChildTaskSubmitter)
  │
  └─ TaskHandlerDeps ──▶ task handlers ──▶ task_handler_manager populated
```

`RunTaskService` references the (currently empty) manager; schedulers reference `RunTaskService`; the router is built from schedulers; handler factories then read a valid `ChildTaskSubmitter`. No task executes before `Dispatcher::Run()`, so the temporarily-empty manager is never observed by `RunTask`.

This requires splitting `BuildTaskHandlerManager(configs, context) -> StatusOr<shared_ptr<Manager>>` into an "own an empty manager" step plus a `TaskHandlerManager::LoadTaskHandlers(configs, deps) -> Status` populate method. That is core-internal and not extension-facing.

Alternative considered: build handlers before schedulers and give the scheduler a late-bound `RunTaskService` slot. Rejected — it re-introduces exactly the late binding this change removes, and it violates `child-task-submission` (handlers get the submitter at construction).

### D3: Placement of the bundles

| Bundle | Location | Namespace |
|---|---|---|
| `NodeSchedulerDeps` | `include/strij/extensions/scheduler.hh` | `strij::nodeagent` |
| `DataDependencyFetcherDeps` | `include/strij/extensions/data_dependency_fetcher.hh` | `strij::nodeagent` |
| `TaskHandlerDeps` | `include/strij/nodeagent/task_handlers.hh` | `strij::nodeagent` |

`TaskHandler`, `ResultSender`, and `TaskHandlerFactory` move from `src/nodeagent/extensions/task_handlers/task_handlers.hh` to `include/strij/nodeagent/task_handlers.hh` so the public nodeagent surface is reachable under the `strij/` include prefix (the current target is public but at a `src/` path). `ChildTaskForwarder` moves from `src/nodeagent/core/child_task_forwarder.hh` to `include/strij/nodeagent/child_task_forwarder.hh`; it is already a pure-abstract port.

`NodeSchedulerDeps` names `DataDependencyFetcherRouter`, which stays a concrete `src/` type forward-declared from the public header. This preserves the existing (accepted) seam; the include-purity check only inspects `#include` lines, so a forward declaration is sufficient.

### D4: Keep the base `FactoryContext` for the gateway

`FactoryContext` and `GatewayFactoryContext` remain; only the nodeagent `NodeagentFactoryContext` is removed. `extension-registry`'s `FactoryContext` requirement is re-scoped to the gateway side.

### D5: Test stub for the forwarder

`NodeSchedulerDeps::child_task_forwarder_` is a reference, so tests that build a scheduler router without a real `GatewayClient` need a stub. Add a small `test/mocks/...` `StubChildTaskForwarder` (returns a configurable `absl::Status`) rather than making the field nullable.

## Risks / Trade-offs

- **[Breaking extension API]** → Intentional and pre-1.0. The consumer smoke test and mocks are updated in the same change; a short migration note is added to the extension author guide.
- **[Large mechanical test churn]** → ~23 loader call sites and the nodeagent factory mocks. Mitigate by adding a test-only fixture/helper that assembles a default `NodeSchedulerDeps`/`TaskHandlerDeps`, so most sites change one line.
- **[Empty manager observed before population]** → Only if something calls `RunTask` during startup. Nothing does; `RunTask` is only reachable from `Dispatcher::Run()`. A debug assertion that the manager is non-empty on first `RunTask` can make this explicit.
- **[`DataDependencyFetcherRouter` remains a concrete public-named type]** → Pre-existing; not worsened. Documented as a follow-up.
- **[Bundle regrowth]** → The per-category bundles could accumulate services over time. Reference members force every construction site to change when a field is added, which keeps growth deliberate.

## Migration Plan

Single atomic change; no runtime migration. Rollback is a revert of the change (the old context and `Set*()` calls are restored). No persisted state, config, or wire format changes.

## Open Questions

None.
