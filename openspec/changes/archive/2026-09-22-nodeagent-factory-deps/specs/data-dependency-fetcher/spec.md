# Spec Delta

## MODIFIED Requirements

### Requirement: Node-global ObjectCache

An `ObjectCache` interface SHALL be defined with `Populate(ref, data)` and `IsCached(ref) -> bool`. The cache SHALL be owned by the node agent's main component and injected into the `DataDependencyFetcherDeps` bundle. Cache key SHALL be source-qualified (`source + ":" + key`). Two tasks sharing a ref SHALL not double-fetch. Cache eviction is a fetcher concern (not defined by this interface).

#### Scenario: Two tasks sharing a ref do not double-fetch

- **WHEN** task A with `deps = [{source: "ray", key: "obj_abc"}]` has its dep fetched
- **AND** task B with the same dep arrives later
- **THEN** `IsCached({source: "ray", key: "obj_abc"})` SHALL return true and no fetch SHALL be initiated for task B

#### Scenario: Cache is task-agnostic

- **WHEN** `IsCached({source: "ray", key: "obj_abc"})` is called
- **THEN** the result SHALL depend only on whether `"ray:obj_abc"` is in the cache, not on which task is querying

### Requirement: DataDependencyFetcher config

`NodeAgentConfig` SHALL gain a `data_dependency_fetchers` list of `ExtensionConfig` entries (optional; empty means no prefetching). Each entry SHALL be looked up in `Registry<DataDependencyFetcherFactory>` and constructed with a `DataDependencyFetcherDeps` bundle exposing the event dispatcher and the shared `ObjectCache`. Startup SHALL fail if a named factory is not found.

#### Scenario: Empty fetcher list is valid

- **WHEN** `data_dependency_fetchers` is empty or absent
- **THEN** the nodeagent SHALL start without prefetch capability and deps on probes SHALL be silently ignored

#### Scenario: Unknown fetcher name fails startup

- **WHEN** a fetcher entry sets `name = "nonexistent"` and no such factory is registered
- **THEN** the nodeagent SHALL log an error and exit with status 1
