# Proposal

## Why

The nodeagent constructs its services and its `NodeagentFactoryContextImpl` in a tangled order, then patches the context with `SetRunTaskService()`, `SetDataDependencyFetcherRouter()`, and `SetChildTaskSubmitter()` after the fact. This two-phase setup hides a real cycle (handler manager → child submitter → scheduler router → run-task service → handler manager) and an input/output knot (the single god context is both consumed by extension factories and produced by them). It has already produced a latent bug: `workflow_task_handler.cc` captures `context.ChildTaskSubmitter()` before `SetChildTaskSubmitter()` runs, storing a reference formed from `nullptr`.

The nodeagent context is also far larger than any one extension family needs: task handler factories see `RunTaskService`/`DataDependencyFetcherRouter`/`ObjectCache` they never use, and scheduler factories see `FunctionResolver`/`ChildTaskSubmitter` they never use.

## What Changes

- Replace `extensions::NodeagentFactoryContext` with three small, per-family dependency bundles of references:
  - `NodeSchedulerDeps` (beside `NodeSchedulerFactory`) — dispatcher, run-task service, admission, child-task forwarder, data-dependency fetcher router.
  - `TaskHandlerDeps` (beside `TaskHandlerFactory`) — dispatcher, function resolver, child-task submitter.
  - `DataDependencyFetcherDeps` (beside `DataDependencyFetcherFactory`) — dispatcher, object cache.
- Change each nodeagent factory's `Create` to take its family's Deps bundle by const reference. **BREAKING** for third-party nodeagent extensions.
- Build the nodeagent service graph in dependency order with no setters: create the `TaskHandlerManager` empty, build `RunTaskService` over it, build fetchers → router, build schedulers → `NodeagentSchedulerRouter`, then build handlers (which now see a real `ChildTaskSubmitter`) and populate the manager. This resolves the cycle and the knot, and makes the workflow null-submitter bug unwritable.
- Delete `NodeagentFactoryContext` and `NodeagentFactoryContextImpl`; keep the base `FactoryContext`/`GatewayFactoryContext` for the (unchanged) gateway side.
- Move `TaskHandlerFactory` (with `TaskHandler`, `ResultSender`) to `include/strij/nodeagent/task_handlers.hh` and `ChildTaskForwarder` to `include/strij/nodeagent/child_task_forwarder.hh`, so the public nodeagent extension surface is reachable under the `strij/` include prefix.
- Add a small test stub `ChildTaskForwarder` for tests that build a scheduler router without a real `GatewayClient`.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `extension-registry`: the abstract `FactoryContext` requirement becomes gateway-scoped; the nodeagent side no longer passes one context to every factory.
- `nodeagent-task-handlers`: `TaskHandlerFactory::Create` takes `TaskHandlerDeps`; the child-submitter requirement is re-expressed against the bundle.
- `node-local-scheduler`: `NodeSchedulerFactory::Create` takes `NodeSchedulerDeps`; the scheduler's available services are enumerated by the bundle.
- `data-dependency-fetcher`: `DataDependencyFetcherFactory::Create` takes `DataDependencyFetcherDeps`; `ObjectCache` is injected through it.
- `child-task-submission`: `ChildTaskSubmitter` is exposed through `TaskHandlerDeps`, and is guaranteed valid at handler-factory `Create` time.
- `function-resolver`: the resolver is exposed through `TaskHandlerDeps` rather than a nodeagent `FactoryContext`.
- `gateway-client`: `ChildTaskForwarder` is exposed through `NodeSchedulerDeps` and declared in `include/`.
- `piped-executable-task-handler`: the factory obtains the resolver from `TaskHandlerDeps`.

## Impact

- Public API: `include/strij/extensions/factory_context.hh`, `scheduler.hh`, `data_dependency_fetcher.hh`; new `include/strij/nodeagent/task_handlers.hh`, `include/strij/nodeagent/child_task_forwarder.hh`; `src/nodeagent/exe/nodeagent_factory_context.{hh,cc}` (deleted); all nodeagent factory implementations and the consumer smoke-test factory.
- Core: `src/nodeagent/core/task_handler_manager.{hh,cc}` (split create/populate), `data_dependency_fetcher_router.{hh,cc}`, `nodeagent_scheduler_router.{hh,cc}`, `src/nodeagent/exe/nodeagent_framework.cc` (ordering).
- Tests: `MockNodeagentFactoryContext` and the nodeagent factory `Create` mocks; ~23 loader call sites across `test/nodeagent/...` and `test/common/extensions/...`.
- Build: `src/nodeagent/extensions/task_handlers/BUILD.bazel` and the new `include/strij/nodeagent` target.
- No runtime behavior change: the same services are wired to the same factories; only the construction order and the API shape change.
