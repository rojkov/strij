## 1. Proto and Event Loop Foundations

- [x] 1.1 Add `DataRef` message to `api/common/task/task.proto`: `source`, `key`, `sha` string fields
- [x] 1.2 Add `repeated DataRef deps = 6` field to `Task` message in `api/common/task/task.proto`
- [x] 1.3 Add `repeated DataRef deps = 4` field to `TaskProbe` message in `api/common/task/probe.proto`
- [x] 1.4 Add `DEP_COMPLETED` to `Command::Type` enum in `include/strij/event/command.hh`
- [x] 1.5 Build and verify proto compilation: `make build`

## 2. ObjectCache Interface

- [x] 2.1 Define `ObjectCache` pure-abstract interface in `include/strij/nodeagent/object_cache.hh` with `Populate(ref, data)` and `IsCached(ref) -> bool`
- [x] 2.2 Add `virtual auto ObjectCache() -> nodeagent::ObjectCache& PURE;` to `NodeagentFactoryContext` in `include/strij/extensions/factory_context.hh`
- [x] 2.3 Forward-declare `ObjectCache` in `include/strij/extensions/factory_context.hh` (inside `namespace strij::nodeagent`)
- [x] 2.4 Add `ObjectCache` accessor to `NodeagentFactoryContextImpl` in `src/nodeagent/exe/nodeagent_factory_context.hh` and `.cc` (stub: create a trivial in-memory implementation for now, or inject a concrete impl later)
- [x] 2.5 Write `ObjectCache` unit test in `test/nodeagent/object_cache_test.cc`

## 3. DataDependencyFetcher Extension Category

- [x] 3.1 Define `DataDependencyFetcher` pure-abstract interface in `include/strij/extensions/data_dependency_fetcher.hh` with `Fetch(ref, task_id, dispatcher, destination)` and `HandledSourceTypes()`
- [x] 3.2 Define `DataDependencyFetcherFactory` in `include/strij/extensions/data_dependency_fetcher.hh` with `Name()`, `CreateEmptyConfigProto()`, `Create(config, NodeagentFactoryContext&)`
- [x] 3.3 Register `Registry<DataDependencyFetcherFactory>` (via existing `extension_registry.hh` template)
- [x] 3.4 Add `REGISTER_FACTORY` / `REGISTER_FACTORY_FULLY_QUALIFIED` support (already provided by `extension_registry.hh` — no additional work needed)

## 4. DataDependencyFetcherRouter

- [x] 4.1 Define `DataDependencyFetcherRouter` in `src/nodeagent/core/data_dependency_fetcher_router.hh` with `FetchAll(deps, task_id, dispatcher, destination)` and `AllCached(deps) -> bool`
- [x] 4.2 Implement router construction: build `source → DataDependencyFetcher*` dispatch table from union of fetchers' `HandledSourceTypes()`; fail on duplicate schemes
- [x] 4.3 Implement `FetchAll`: iterate deps, dispatch each to owning fetcher's `Fetch`
- [x] 4.4 Implement `AllCached`: iterate deps, return true iff all `ObjectCache::IsCached` returns true
- [x] 4.5 Write unit test in `test/nodeagent/data_dependency_fetcher_router_test.cc` with mock fetchers

## 5. Config Schema

- [x] 5.1 Add `ObjectCacheConfig` message to `api/nodeagent/config/nodeagent.proto` with `max_entries` and `default_ttl`
- [x] 5.2 Add `repeated ExtensionConfig data_dependency_fetchers = 12` and `ObjectCacheConfig object_cache = 13` to `NodeAgentConfig` (10/11 were taken by `heartbeat_interval`)
- [x] 5.3 Wire `data_dependency_fetchers` loading in nodeagent startup: look up each named factory in `Registry<DataDependencyFetcherFactory>`, construct fetchers, build router; fail on unknown name

## 6. Gateway Probe Scheduler — deps in probe

- [x] 6.1 In `src/gateway/extensions/schedulers/probe/probe_scheduler.cc` `serializeTaskProbe()`: copy `task.deps()` into the `TaskProbe` message before serialization

## 7. Probe Local Scheduler Integration

- [x] 7.1 Extend `ProbeLocalScheduler` constructor to accept `DataDependencyFetcherRouter&` (in addition to existing `RunTaskService&` and `AdmissionControllerSharedPtr`) plus `event::Dispatcher&` (needed to drive `FetchAll`); the router owns the readiness/cache query, so no separate `ObjectCache&` is injected
- [x] 7.2 In `handleProbe()`: call `router.FetchAll(probe.deps(), probe.id(), dispatcher, this)` to initiate prefetch after admit-or-enqueue
- [x] 7.3 In `walk()`: before pulling a queued probe, check `router_.AllCached(probe.deps())` — only pull if deps are cached (empty deps = always cached; unknown-source refs never gate)
- [x] 7.4 In `ProcessCommand()`: handle `DEP_COMPLETED` — extract task id from `args_`, call `walk()` to re-evaluate readiness for the relevant task
- [x] 7.5 Update `ProbeLocalSchedulerFactory::Create` to inject `DataDependencyFetcherRouter`, `ObjectCache`, and `Dispatcher` from `NodeagentFactoryContext`

## 8. Tests

- [x] 8.1 Write `ProbeLocalScheduler` unit test: probe with deps enqueued, fetch starts, `DEP_COMPLETED` triggers walk, probe pulled when deps cached
- [x] 8.2 Write `ProbeLocalScheduler` unit test: probe with deps enqueued, capacity freed (`CAPACITY_RELEASED`), but deps not cached — probe stays queued
- [x] 8.3 Write `ProbeLocalScheduler` unit test: probe with empty deps — behaves identically to Phase-2 (no fetch, pull on capacity)
- [x] 8.4 Write `ProbeLocalScheduler` unit test: "deps already cached" short-circuit — `IsCached` returns true on probe arrival, pull proceeds immediately
- [x] 8.5 Write `ProbeLocalScheduler` unit test: `DEP_COMPLETED` for unknown task id is a no-op
- [x] 8.6 Write gateway `ProbeScheduler` unit test: `serializeTaskProbe` includes `deps` from task
- [x] 8.7 Run full test suite: `make test`

## 9. Config and Startup Validation

- [x] 9.1 Validate at startup: unknown `data_dependency_fetchers` name fails with error
- [x] 9.2 Validate at startup: duplicate source scheme across fetchers fails with error
- [x] 9.3 Empty `data_dependency_fetchers` list: nodeagent starts without prefetch capability, deps on probes silently ignored
