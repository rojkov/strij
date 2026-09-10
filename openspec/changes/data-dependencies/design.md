## Context

Phases 0–2 of `2026-08-30-pluggable-task-scheduling` are landed:

- **Phase 0**: `Scheduler` interface (`Schedule`/`HandleFrame`/`RequiredProtocol`), split `FactoryContext` (gateway + nodeagent), `SchedulerRouter` for per-type dispatch, `PushLocalScheduler`, nodeagent frame dispatcher (`HandledFrameTypes()` routing), `RunTask` service, config schema.
- **Phase 1**: `CAPACITY_RELEASED` via `ProcessCommand`, `AdmissionController::RegisterCapacityObserver`, bounded queue (`BoundedQueue<T>`), `CommandHandler` role on schedulers.
- **Phase 2**: Full probe protocol — gateway `ProbeScheduler` (candidate sampling, race arbitration, deadline sweep), nodeagent `ProbeLocalScheduler` (queue-then-preallocate, `TaskProbe`/`TaskPull`/`TaskGrant`/`TaskDecline`/`TaskProbeCancel`), deferred admission.

The probe local scheduler's `handleProbe` admits or enqueues on probe arrival; `walk()` drains the queue on `CAPACITY_RELEASED`. When a task sits in the queue, the node has idle bandwidth but no knowledge of the task's data dependencies — fetching is entirely serial with execution after grant.

The design for Phase 3 was sketched in the `2026-08-30-pluggable-task-scheduling/design.md` (Future phases section). This document resolves the open decisions from that sketch and specifies the implementation.

## Goals / Non-Goals

**Goals:**
- `DataRef` proto: `{source, key, sha}` — carries the fetch backend type and opaque identifiers for a remote data object.
- `Task.deps`: `repeated DataRef` on the `Task` proto — an optimistic prefetch hint, not a contract. Clients declare it at HTTP submission time; handlers declare it for child tasks (Phase 4). The scheduler prefetches based on these refs; if a handler discovers additional refs inside `Task.body` at run time, those are not pre-fetched.
- `TaskProbe.deps`: copied from `Task.deps` at gateway probe construction time, so the node knows which refs to fetch at enqueue.
- `DataDependencyFetcher` extension category: source-keyed (e.g. `"ray"`, `"http"`, `"local"`), pluggable fetch backend, created with `NodeagentFactoryContext`. Fetcher owns the I/O mechanics; the scheduler owns the readiness policy.
- Node-global `ObjectCache`: queryable by fetchers (`Populate`), by schedulers (`IsCached`), and by the completion path. Cache eviction is a fetcher concern.
- `DEP_COMPLETED` command: new `Command::Type`. Fetcher submits `{type: DEP_COMPLETED, destination_: scheduler, args_: &task_id}` via the dispatcher on each dep completion. The scheduler re-evaluates readiness: `preallocated ∧ deps_cached ∧ grant_received`.
- Probe local scheduler readiness gate: on probe arrival, `handleProbe` starts fetches via the source-router and calls `walk()`; `walk()` checks `IsCached` for each queued probe's deps before pulling; `ProcessCommand(DEP_COMPLETED)` re-triggers `walk()`.
- "Deps already cached" short-circuits: `IsCached` returns true at probe arrival → skip fetch, readiness satisfied immediately.

**Non-Goals:**
- Gateway task-locality routing (push task to the node that holds the data) — this requires a cluster-wide object-location registry, which is a scheduling-policy concern for a later phase.
- Cluster-aware node-local schedulers that bypass the gateway — future work.
- Handler-surface changes (`HandleTask` gains submission handle) — Phase 4.
- Cache eviction policy design — defined by fetcher implementations, not this change.
- Cluster-wide object registry — depends on node-to-gateway state reporting evolution.
- S3/Ray/local fetcher implementations — the extension category and seams land here; concrete backends are separate changes.

## Decisions

### D1: DataRef proto — source-keyed, scheme-dispatched

```proto
message DataRef {
  string source = 1;  // fetch backend scheme: "ray", "http", "local"
  string key = 2;     // opaque identifier within the source (object id, URL, path)
  string sha = 3;     // content hash for integrity verification (optional)
}
```

`source` is the discriminator. A `DataDependencyFetcherRouter` on the node dispatches each `DataRef` to the fetcher that owns its `source` scheme (symmetrical with the frame dispatcher's `HandledFrameTypes()` routing). Two fetchers may coexist on a node (e.g. `"ray"` + `"http"`), each owning a disjoint set of schemes.

- *Alternative considered*: URL-encoded source in `key` (e.g. `s3://bucket/file`). Rejected — loses the clean scheme dispatch, complicates probe parsing, and makes the `source` field harder to route.
- *Alternative considered*: type-resolved reference (no `source`, just `key`, with fetcher determined by global config). Rejected — loses self-describing probes, makes coexistence of multiple sources harder, and complicates cache key namespacing.

### D2: Task.deps — optimistic hint, not contract

```proto
message Task {
  string id = 1;
  string type = 2;
  bytes body = 3;
  map<string, string> parameters = 4;
  strij.node.ResourceRequirements requirements = 5;
  repeated DataRef deps = 6;  // prefetch hints — best-effort, not a contract
}
```

`deps` is populated at two points:
1. **HTTP submission**: the client provides deps via a structured HTTP header (e.g. `X-Strij-Deps: [{"source":"ray","key":"obj_abc"}, ...]`). The gateway copies them into `Task.deps`.
2. **Handler-submitted child tasks (Phase 4)**: the handler parses its own body and declares deps when submitting child tasks.

If a handler discovers additional refs inside `Task.body` at run time, those are not pre-fetched. This is an accepted limitation: `deps` is a scheduler optimization hint. The scheduler may prefetch zero, some, or all of the task's actual data dependencies.

- *Alternative considered*: gateway parses the body per task-type to extract refs. Rejected — the gateway is a routing shim; it does not interpret body semantics. Each new task type would need a gateway-side parser.
- *Alternative considered*: no `deps` field; handler declares deps at run time. Rejected — defeats the purpose of Phase 3 (no overlap between fetch and queue wait).

### D3: TaskProbe.deps — copied from Task

```proto
message TaskProbe {
  string id = 1;
  string type = 2;
  strij.node.ResourceRequirements requirements = 3;
  repeated DataRef deps = 4;  // copied from Task.deps at probe construction
}
```

The gateway `ProbeScheduler` copies `task.deps()` into the probe when constructing the `TaskProbe` frame. This is a trivial addition to `serializeTaskProbe()`. Old nodes without Phase 3 ignore the new field (proto3 forward compatibility).

### D4: Node-global ObjectCache — owned by node agent main, injected via context

```cpp
class ObjectCache {
public:
  virtual ~ObjectCache() = default;

  // Populates the cache with data for a ref. Called by fetchers on completion.
  virtual void Populate(const task::DataRef& ref, absl::string_view data) = 0;

  // Returns true if the data for this ref is fully cached and available.
  [[nodiscard]] virtual auto IsCached(const task::DataRef& ref) const -> bool = 0;
};
```

The cache lives in `NodeagentFactoryContext`:

```cpp
class NodeagentFactoryContext : public FactoryContext {
public:
  virtual auto FunctionResolver() -> nodeagent::FunctionResolver& PURE;
  virtual auto AdmissionController() -> nodeagent::AdmissionControllerSharedPtr PURE;
  virtual auto RunTaskService() -> nodeagent::RunTaskService& PURE;
  virtual auto ObjectCache() -> nodeagent::ObjectCache& PURE;
};
```

Cache key = `source + ":" + key` (source-qualified, task-agnostic). Two tasks sharing a ref don't double-fetch. Cache eviction is a fetcher concern (the cache provides the interface; fetchers implement per-source eviction policy, e.g. LRU for `"ray"`, TTL for `"http"`).

- *Alternative considered*: per-fetcher caches. Rejected — the scheduler queries readiness for all deps; a single `IsCached` query point is cleaner. Also, a per-fetcher cache cannot detect "ref already cached by another backend" (e.g. a ref fetched via HTTP, now also needed by a handler expecting `"local"`).
- *Alternative considered*: fetcher owns the cache, injects it into context. Rejected — multiple fetchers would need shared state; a single global cache is simpler.

### D5: DEP_COMPLETED command — extends the Phase-1 mechanism

```cpp
struct Command {
  enum Type : std::uint8_t {
    ACTIVATE_READ,
    DEFERRED_DELETE,
    CAPACITY_RELEASED,
    DEP_COMPLETED,        // ← new
  } type_{};
  CommandHandler* destination_{nullptr};
  void* args_{nullptr};   // for DEP_COMPLETED: points to a stable std::string* (task_id)
};
```

When a fetch completes:
1. The fetcher calls `ObjectCache::Populate(ref, data)`.
2. The fetcher submits `{type: DEP_COMPLETED, destination_: scheduler_handler_, args_: &task_id}` via the dispatcher.
3. The scheduler's `ProcessCommand(DEP_COMPLETED)` calls `walk()`, which re-evaluates readiness for all queued and preallocated probes.

`args_` carries a pointer to a `std::string` (task id) that must outlive the command delivery. Since the scheduler owns the `QueuedProbe` / `PullPending` entries keyed by task id, the string is stable. If the task has already been granted or cancelled, the command is a no-op (idempotent).

- *Alternative considered*: `args_ == nullptr` (pure wakeup, like `CAPACITY_RELEASED`), scheduler re-checks all queued probes. Rejected — wasteful when many tasks are queued; the scheduler should only re-evaluate the specific task whose dep completed.
- *Alternative considered*: per-ref callback (`std::function`). Rejected — needs lifetime tracking (scheduler destroyed mid-fetch); `Command` already solves this via destination pointer and command queue drain.

### D6: DataDependencyFetcher extension category

```cpp
class DataDependencyFetcher {
public:
  virtual ~DataDependencyFetcher() = default;

  // Starts fetching data for `ref`. If already cached, synthesizes DEP_COMPLETED
  // immediately (short-circuit). Fetcher populates the cache on completion and
  // then submits DEP_COMPLETED via the dispatcher.
  virtual void Fetch(const task::DataRef& ref, const std::string& task_id,
                     event::Dispatcher& dispatcher, event::CommandHandler* destination) = 0;

  // The source schemes this fetcher handles (e.g. {"ray"}, {"http"}).
  [[nodiscard]] virtual auto HandledSourceTypes() const -> std::span<const std::string_view> = 0;
};
```

The `DataDependencyFetcherRouter` builds a `source → DataDependencyFetcher*` dispatch table from the union of all configured fetchers' `HandledSourceTypes()`. Schemes are ownership-disjoint (like frame type ids).

```cpp
class DataDependencyFetcherRouter {
public:
  // Fetches all deps for a task. For each ref, dispatches to the owning fetcher.
  void FetchAll(const google::protobuf::RepeatedPtrField<task::DataRef>& deps,
                const std::string& task_id, event::Dispatcher& dispatcher,
                event::CommandHandler* destination);

  // Returns true if ALL deps are in the cache.
  [[nodiscard]] auto AllCached(const google::protobuf::RepeatedPtrField<task::DataRef>& deps) const -> bool;
};
```

### D7: Readiness gate — `preallocated ∧ all_deps_cached ∧ grant_received`

The probe local scheduler's readiness predicate becomes three-operand. A probe becomes runnable when:

```
preallocated == true            (kTaskPull already sent, AdmissionScope held)
∧ all_deps_cached == true      (ObjectCache::IsCached returns true for every ref)
∧ grant_received == true       (kTaskGrant received; not needed in v1 since pull=preallocate)
```

In the current queue-then-preallocate model, preallocation and pull happen together (on walk). The grant is received after pull. So the readiness gate in practice is:

1. **On probe arrival** (`handleProbe`): check capacity via `Admit` → if ok, preallocate + send pull. Independently, call `FetchAll(probe.deps(), ...)`. Readiness is "deps cached ∧ grant received" (capacity already satisfied).
2. **On walk** (`walk`): for each queued probe, check `Admit` AND `AllCached(probe.deps())` before pulling. Both must be true.
3. **On `DEP_COMPLETED`** (`ProcessCommand`): re-trigger `walk()` — the newly cached dep may unblock a queued probe.
4. **On `CAPACITY_RELEASED`** (`ProcessCommand`): re-trigger `walk()` — freed capacity may unblock a queued probe whose deps are cached.

A probe that has no deps (`deps` is empty) is "all deps cached" by definition — it behaves exactly as today.

### D8: Fetcher completion path — DEP_COMPLETED wiring

```
handleProbe(probe):
  1. Admit(probe.type, probe.requirements) → preallocate or enqueue
  2. router_.FetchAll(probe.deps(), probe.id(), dispatcher_, this)
  3. sendPull()

router_.FetchAll:
  for each ref in deps:
    fetcher = route(ref.source)
    fetcher.Fetch(ref, task_id, dispatcher_, this)  // this = scheduler as CommandHandler

fetcher.Fetch:
  if cache_.IsCached(ref):
    // short-circuit: submit DEP_COMPLETED immediately
    dispatcher_.SubmitCommand(DEP_COMPLETED, destination_, &stable_task_id)
  else:
    // start async fetch
    async_fetch(ref, [&](data) {
      cache_.Populate(ref, data)
dispatcher_.SubmitCommand(DEP_COMPLETED, destination_, &stable_task_id)
    })
```

### D9: Config schema

```proto
message NodeAgentConfig {
  // ... existing fields ...
  repeated ExtensionConfig schedulers = 9 [(strij.config.required) = true];
  repeated ExtensionConfig data_dependency_fetchers = 10;  // optional; empty = no prefetch
  ObjectCacheConfig object_cache = 11;
}

message ObjectCacheConfig {
  uint64 max_entries = 1;       // LRU eviction when exceeded; 0 = unlimited
  google.protobuf.Duration default_ttl = 2;  // per-ref TTL; 0 = no expiry
}
```

An empty `data_dependency_fetchers` list means no prefetching occurs — `deps` on tasks are silently ignored. Probes still carry `deps` (the proto field exists), but the scheduler never calls `FetchAll` and `AllCached` always returns true for empty deps. This is a valid v1 config: existing probe-only deployments opt in explicitly.

Each `data_dependency_fetchers` entry is a standard `ExtensionConfig`:
```yaml
data_dependency_fetchers:
  - name: "ray"
    extension:
      name: "ray"
      config:
        registry_address: "ray://gcs:6381"
  - name: "http"
    extension:
      name: "http"
      config:
        max_concurrent_downloads: 8
```

## Risks / Trade-offs

- **`deps` is a hint — no guarantee of pre-fetch completeness.** A handler may find additional refs in `Task.body` not declared in `deps`. These are fetched at run time, not pre-fetched. Mitigation: documentation; handlers should declare all refs they know about. Phase 4 handlers can inspect their own body schema.
- **`args_` pointer in `DEP_COMPLETED` must be stable.** The task id string must outlive the command queue drain. Mitigation: the string lives in `QueuedProbe.probe` or `PullPending.probe`, both owned by the scheduler's maps. If the entry is erased before the command fires, `ProcessCommand` is a no-op (the task id is not found).
- **Proto forward compatibility: old nodes ignore `deps`.** If a gateway runs Phase 3 but a nodeagent does not, `TaskProbe.deps` is silently ignored. No functional break, but no prefetch. If a nodeagent runs Phase 3 but the gateway does not, `deps` is empty — no prefetch, no break. The wire is forward-compatible.
- **Cache memory growth.** Without eviction, the node-global cache grows monotonically. Mitigation: `ObjectCacheConfig` with `max_entries` and `default_ttl`; concrete eviction policy is a fetcher concern.
- **Fetcher I/O blocking the event loop.** A naive fetcher (e.g. synchronous HTTP) would block the single event-loop thread. Mitigation: fetcher implementations must use async I/O (io_uring, libcurl multi, etc.). This is enforced by the extension contract: `Fetch` must return immediately; completion is async via command dispatch.
- **Multiple fetchers for the same source.** `HandledSourceTypes()` must be disjoint across fetchers (like frame type ids). Startup validation: fail if two fetchers claim the same source scheme.

## Migration Plan

1. **Proto-only change (safe to land first)** — add `DataRef` message, `Task.deps`, `TaskProbe.deps`. Existing gateways and nodeagents ignore the new fields. No behavioral change.
2. **Nodeagent-side seams** — `ObjectCache` interface, `NodeagentFactoryContext::ObjectCache()`, `DataDependencyFetcher` + `DataDependencyFetcherRouter`, `DEP_COMPLETED` command type. No behavioral change until a fetcher is configured and the probe scheduler calls `FetchAll`.
3. **Probe scheduler integration** — `handleProbe` calls `FetchAll`, `walk` checks `AllCached`, `ProcessCommand(DEP_COMPLETED)` re-triggers walk. Opt-in via config: `data_dependency_fetchers` empty = no change.
4. **Gateway-side** — trivial: copy `task.deps()` into `TaskProbe.deps` in `serializeTaskProbe()`. No behavioral change.
5. **Rollback** — remove `data_dependency_fetchers` from config → no fetchers configured → deps ignored, probe scheduler behaves as before. Proto fields remain but are unused.

## Open Questions

1. **Fetcher I/O model for concrete backends.** `HttpDataFetcher` and `RayDataFetcher` need async I/O. The exact mechanism (io_uring send/recv, libcurl multi, etc.) is deferred to the backend implementation changes.
2. **Cache eviction policy.** The `ObjectCacheConfig` knobs are defined; the actual eviction logic is a fetcher/cache implementation detail.
3. **HTTP header format for `deps` at submission time.** The proposal specifies `X-Strij-Deps` with JSON; the exact schema (JSON array of `{source, key, sha}` objects) is a gateway HTTP handler concern.
