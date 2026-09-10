## Why

Tasks whose inputs are remote data objects currently cannot begin fetching those objects until execution. With the probe protocol (Phase 2), a task may sit in a node's admission queue for a non-trivial time before capacity is available. During that wait, the node has idle bandwidth and the task's data dependencies are known but not fetched. Overlapping fetch with queue wait makes the task runnable the moment it is granted — eliminating a serial fetch-then-execute bottleneck that becomes material at cluster scale.

This is Phase 3 of the `2026-08-30-pluggable-task-scheduling` change.

## What Changes

- **`DataRef` proto** — new message `{source, key, sha}` added to `api/common/task/`. `source` identifies the fetch backend (e.g. `"ray"`, `"http"`, `"local"`); `key` and `sha` are opaque identifiers resolved by the fetcher. Placed alongside `Task` so both `Task` and `TaskProbe` carry it.
- **`Task.deps` field** — `repeated DataRef deps` added to the `Task` proto. An **optimistic prefetch hint**, not a contract. Clients declare it at submission time (HTTP header); handlers will declare it for child tasks (Phase 4). The scheduler prefetches based on these refs. If a handler discovers additional refs inside `Task.body` at run time, those are not pre-fetched — this is an accepted best-effort limitation.
- **`TaskProbe.deps` field** — `repeated DataRef deps` added to `TaskProbe`. Copied from `Task.deps` when the gateway constructs the probe, so the node knows which refs to begin fetching while the task sits in the queue.
- **`DataDependencyFetcher` extension category** — new factory interface (`Registry<DataDependencyFetcherFactory>`) created with `NodeagentFactoryContext`. A fetcher declares which `source` schemes it handles via `HandledSourceTypes()` (symmetrical with `Scheduler::HandledFrameTypes()`). The fetcher owns the actual I/O mechanics: check the node-global cache → if missing, fetch → populate cache → signal completion. Implementations: `RayDataFetcher`, `HttpDataFetcher` (S3-like), `LocalDataFetcher` (NFS / host copy).
- **`DataDependencyFetcherRouter`** — dispatches each `DataRef` to the fetcher owning its `source` scheme. Symmetrical with the nodeagent frame dispatcher's `HandledFrameTypes()` routing.
- **Node-global object cache** — owned by the node agent's main component, injected into `NodeagentFactoryContext` as `ObjectCache&`. Queryable by fetchers (`Populate(ref, data)`), by schedulers (`IsCached(ref) -> bool`), and by the fetcher's completion path. All fetchers populate the same cache. Cache eviction is a fetcher concern (per-node policy).
- **`DEP_COMPLETED` command** — new `Command::Type` in the event loop. When a fetch completes, the fetcher submits `{type: DEP_COMPLETED, destination_: scheduler, args_: task_id}` via the dispatcher. The probe scheduler's `ProcessCommand` re-evaluates readiness: `preallocated ∧ all_deps_cached ∧ grant_received`. "Deps already cached" short-circuits instantly — `IsCached` returns true on probe arrival and the scheduler skips fetching entirely.
- **Probe local scheduler readiness gate** — `handleProbe` calls `Fetch` on each dep via the source-router (start prefetch immediately on enqueue). `walk()` checks `IsCached` for each queued probe's deps before pulling; `ProcessCommand(DEP_COMPLETED)` re-triggers `walk()`.

## Capabilities

### New Capabilities

- `data-dependency-fetcher`: The `DataDependencyFetcher` extension category, source-router, node-global object cache (`ObjectCache`), `DataRef` proto, and `DEP_COMPLETED` command plumbing. Covers the mechanism that overlaps data prefetch with admission queue wait.
- `object-cache`: Node-global object cache interface — populate, query (`IsCached`), and subscription/completion notification surface. Injected via `NodeagentFactoryContext`, shared by all fetchers.

### Modified Capabilities

- `probe-scheduling`: `TaskProbe` gains a `deps` field; the node-side probe local scheduler gains the readiness gate (`preallocated ∧ deps_cached ∧ grant_received`); `ProcessCommand` handles `DEP_COMPLETED`. `Task.deps` is the source the gateway copies into the probe.

## Impact

- **Protos**: `api/common/task/task.proto` (new `DataRef` message, `Task.deps` field), `api/common/task/probe.proto` (`TaskProbe.deps` field).
- **Event loop**: new `Command::Type::DEP_COMPLETED` in `common/core/event/command.hh`.
- **Nodeagent factory context**: new `ObjectCache&` accessor added to `NodeagentFactoryContext` (in `include/strij/extensions/factory_context.hh`); concrete context impls construct and own the cache.
- **Extensions**: new `DataDependencyFetcherFactory` + `Registry<DataDependencyFetcherFactory>`; new `ObjectCache` interface in `include/strij/` (public); new `DataDependencyFetcherRouter` in `src/nodeagent/core/`.
- **Probe local scheduler**: `src/nodeagent/extensions/schedulers/probe/probe_local_scheduler.cc` — gains dep-fetching logic in `handleProbe`/`walk`/`ProcessCommand`.
- **Gateway probe scheduler**: `src/gateway/extensions/schedulers/probe/probe_scheduler.cc` — copies `task.deps()` into `TaskProbe.deps` (trivial).
- **Config**: `NodeAgentConfig` gains an `object_cache` section (capacity, eviction policy knobs); `NodeAgentConfig` gains a `data_dependency_fetchers` list (name + config per source scheme).
- **No wire-breaking changes**: `Task.deps` and `TaskProbe.deps` are new fields; old nodes without Phase 3 ignore them (proto3 forward compatibility). Gateway and nodeagent roll independently.
