## ADDED Requirements

### Requirement: DataRef proto

A `DataRef` message SHALL be defined in `api/common/task/` with three string fields: `source` (the fetch backend scheme, e.g. `"ray"`, `"http"`, `"local"`), `key` (opaque identifier within the source), and `sha` (optional content hash for integrity verification). `DataRef` SHALL be used by `Task.deps` and `TaskProbe.deps`.

#### Scenario: DataRef carries scheme and key

- **WHEN** a `DataRef` is constructed for a Ray object with id `obj_abc`
- **THEN** the `source` field SHALL be `"ray"` and the `key` field SHALL be `"obj_abc"`

#### Scenario: DataRef sha is optional

- **WHEN** a `DataRef` has no content hash
- **THEN** the `sha` field SHALL be empty

### Requirement: Task.deps prefetch hint

`Task` SHALL carry a `repeated DataRef deps` field. This field is an **optimistic prefetch hint** — the scheduler prefetches based on these refs on a best-effort basis. The field is not a contract: a handler may discover additional refs inside `Task.body` at run time; those are not pre-fetched. An empty `deps` field means no prefetchable dependencies are declared.

#### Scenario: Client declares deps at HTTP submission

- **WHEN** an HTTP client submits a task with a header containing two `DataRef`s
- **THEN** the gateway SHALL populate `Task.deps` with those two refs and copy them into the `TaskProbe` when constructing a probe

#### Scenario: Empty deps means no prefetch

- **WHEN** a task has no `deps` field (or an empty list)
- **THEN** the scheduler SHALL treat the task as having zero prefetchable dependencies and SHALL NOT initiate any fetch

### Requirement: TaskProbe.deps

`TaskProbe` SHALL carry a `repeated DataRef deps` field. The gateway probe scheduler SHALL copy `task.deps()` into the `TaskProbe.deps` field when constructing the probe frame. Old nodeagents that do not implement Phase 3 SHALL ignore this field (proto3 forward compatibility).

#### Scenario: Probe carries deps from task

- **WHEN** a task with `deps = [{source: "ray", key: "obj_abc"}]` is probed
- **THEN** the `TaskProbe` frame SHALL carry `deps = [{source: "ray", key: "obj_abc"}]`

#### Scenario: Old nodeagent ignores deps

- **WHEN** a Phase-3 gateway sends a probe with `deps` to a Phase-2 nodeagent
- **THEN** the nodeagent SHALL ignore the `deps` field and process the probe as before (no prefetch)

### Requirement: DataDependencyFetcher interface

A `DataDependencyFetcher` SHALL be defined with: `Fetch(ref, task_id, dispatcher, destination)` (start fetching; if cached, synthesize `DEP_COMPLETED` immediately) and `HandledSourceTypes() -> span<const string_view>` (the source schemes this fetcher handles). A fetcher SHALL populate the node-global `ObjectCache` on completion and then submit a `DEP_COMPLETED` command via the dispatcher. `Fetch` SHALL return immediately; fetching is asynchronous.

#### Scenario: Fetcher handles a known source

- **WHEN** a fetcher declares `HandledSourceTypes() == {"ray"}`
- **THEN** it SHALL be the only fetcher registered for the `"ray"` source scheme

#### Scenario: Fetcher short-circuits on cache hit

- **WHEN** `Fetch` is called and `ObjectCache::IsCached(ref)` returns true
- **THEN** the fetcher SHALL submit `DEP_COMPLETED` immediately without performing any I/O

#### Scenario: Fetcher populates cache on completion

- **WHEN** an async fetch completes successfully
- **THEN** the fetcher SHALL call `ObjectCache::Populate(ref, data)` and then submit `DEP_COMPLETED`

### Requirement: DataDependencyFetcher scheme router

A `DataDependencyFetcherRouter` SHALL dispatch each `DataRef` to the fetcher owning its `source` scheme. The router SHALL be constructed at startup from the union of all configured fetchers' `HandledSourceTypes()`. Scheme ownership SHALL be disjoint (no two fetchers may handle the same scheme); startup SHALL fail if a duplicate scheme is detected.

#### Scenario: Each ref routes to the correct fetcher

- **WHEN** two fetchers are configured: `"ray"` and `"http"`
- **THEN** a ref with `source: "ray"` SHALL be dispatched to the `"ray"` fetcher and a ref with `source: "http"` SHALL be dispatched to the `"http"` fetcher

#### Scenario: Unknown source fails startup

- **WHEN** a `DataRef` has `source: "nfs"` but no fetcher handles `"nfs"`
- **THEN** the router SHALL fail at startup with an error naming the unhandled scheme

### Requirement: Node-global ObjectCache

An `ObjectCache` interface SHALL be defined with `Populate(ref, data)` and `IsCached(ref) -> bool`. The cache SHALL be owned by the node agent's main component and injected into `NodeagentFactoryContext`. Cache key SHALL be source-qualified (`source + ":" + key`). Two tasks sharing a ref SHALL not double-fetch. Cache eviction is a fetcher concern (not defined by this interface).

#### Scenario: Two tasks sharing a ref do not double-fetch

- **WHEN** task A with `deps = [{source: "ray", key: "obj_abc"}]` has its dep fetched
- **AND** task B with the same dep arrives later
- **THEN** `IsCached({source: "ray", key: "obj_abc"})` SHALL return true and no fetch SHALL be initiated for task B

#### Scenario: Cache is task-agnostic

- **WHEN** `IsCached({source: "ray", key: "obj_abc"})` is called
- **THEN** the result SHALL depend only on whether `"ray:obj_abc"` is in the cache, not on which task is querying

### Requirement: DEP_COMPLETED command

A `DEP_COMPLETED` command type SHALL be added to the event loop's `Command::Type` enum. When a fetcher completes a fetch, it SHALL submit `{type: DEP_COMPLETED, destination_: scheduler_handler, args_: &task_id}` via the dispatcher. The `args_` field SHALL point to a stable `std::string` (the task id) that outlives the command delivery. The scheduler's `ProcessCommand(DEP_COMPLETED)` SHALL re-evaluate readiness for the specified task.

#### Scenario: Command triggers readiness re-evaluation

- **WHEN** a `DEP_COMPLETED` command arrives for task T
- **THEN** the scheduler SHALL re-evaluate whether task T's deps are all cached and whether capacity is available

#### Scenario: Command for unknown task is a no-op

- **WHEN** a `DEP_COMPLETED` command arrives for a task id that is no longer in the scheduler's queue or pending map
- **THEN** the scheduler SHALL treat it as a no-op

### Requirement: Probe local scheduler prefetch-on-enqueue

The probe local scheduler SHALL, on probe arrival, initiate fetching for all deps declared in the probe via the `DataDependencyFetcherRouter`. The scheduler SHALL check `ObjectCache::IsCached` for each dep before pulling (in `walk`). The scheduler SHALL handle `DEP_COMPLETED` commands by re-triggering `walk()`, which may now find newly cached deps and permit the pull.

#### Scenario: Deps are fetched at enqueue time

- **WHEN** a probe arrives with `deps = [{source: "ray", key: "obj_abc"}]` and capacity is exhausted
- **THEN** the node SHALL enqueue the probe AND initiate a fetch for the dep

#### Scenario: Walk checks deps are cached before pulling

- **WHEN** `walk()` encounters a queued probe whose capacity fits but deps are not all cached
- **THEN** the node SHALL NOT pull the probe yet and SHALL keep it queued

#### Scenario: Deps cached triggers walk

- **WHEN** a `DEP_COMPLETED` command arrives and a queued probe's deps are now all cached and capacity fits
- **THEN** the node SHALL admit and pull the probe

#### Scenario: No-deps probe behaves as before

- **WHEN** a probe arrives with an empty `deps` list
- **THEN** the scheduler SHALL treat it as having zero prefetchable dependencies and SHALL pull as soon as capacity is available (identical to Phase-2 behavior)

### Requirement: DataDependencyFetcher config

`NodeAgentConfig` SHALL gain a `data_dependency_fetchers` list of `ExtensionConfig` entries (optional; empty means no prefetching). Each entry SHALL be looked up in `Registry<DataDependencyFetcherFactory>` and constructed with `NodeagentFactoryContext`. Startup SHALL fail if a named factory is not found.

#### Scenario: Empty fetcher list is valid

- **WHEN** `data_dependency_fetchers` is empty or absent
- **THEN** the nodeagent SHALL start without prefetch capability and deps on probes SHALL be silently ignored

#### Scenario: Unknown fetcher name fails startup

- **WHEN** a fetcher entry sets `name = "nonexistent"` and no such factory is registered
- **THEN** the nodeagent SHALL log an error and exit with status 1