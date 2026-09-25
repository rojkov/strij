# Design

## Context

See proposal.md - Why. Two loader conventions coexist in the shared tree:

```
src/common/
├── extensions/                 # "shared pluggable, alwayslink-registered extensions"
│   ├── scheduler.cc            # pure loader glue, misleadingly named
│   ├── scheduler_loader.hh     # opens strij::gateway + strij::nodeagent  (allowlisted)
│   └── evaluators/             # the only genuine extension impl left here
└── loaders/                    # the spec-sanctioned home
    └── evaluator_loader.{hh,cc}  → strij::loaders::CreateEvaluator
```

The scheduler category is side-split (`GatewaySchedulerFactory::Create(const Message&, GatewayFactoryContext&)` vs `NodeSchedulerFactory::Create(const Message&, const NodeSchedulerDeps&)`, two registries), unlike the single-factory evaluator category. Its loader body is already one private template (`createSchedulerFromExtension<FactoryT, ContextT>`); only the two public wrappers and their side namespaces are duplicated. `GatewayFactoryContext` is a service locator; `NodeSchedulerDeps` is a heavy reference bundle — the context/factory split is intrinsic and not collapsible without widening the public factory interfaces.

## Goals / Non-Goals

**Goals:**

- Bring the scheduler loader into conformance with `extension-api-surface` (dir `src/common/loaders/`, bare `strij::loaders` namespace).
- Express the two side loaders as one public name selected by the dependency/context argument type.
- Shrink `CROSS_CUTTING_ALLOWLIST` by removing the loader's side-namespace bleed.
- Keep error semantics, `typed_config` tolerance, and null-rejection byte-for-byte identical.

**Non-Goals:**

- Merging the two registries or the two factory interfaces (deliberate side split).
- Overload-set unification with other loader categories; a shared `ResolveExtensionConfig<FactoryT>` helper is a deferred investigation (D6).
- Any public `include/` surface change or proto change.

## Decisions

### D1. Move and rename `scheduler.cc` → `src/common/loaders/scheduler_loader.cc`

The file contains only loader glue — the `Scheduler` contract already lives in `include/strij/extensions/scheduler.hh`. The current `scheduler.cc` / `scheduler_loader.hh` name split is an artifact of the loader having been defined next to the (since-relocated) contract. Moving both files into `src/common/loaders/` and naming the `.cc` after its header removes the mismatch and gives the pair the same shape as `evaluator_loader.{hh,cc}`.

After the move, `src/common/extensions/` contains only `evaluators/`, so its top-level `BUILD.bazel` (whose sole target is the moved loader) is deleted.

### D2. One public name: `strij::loaders::CreateScheduler`, two overloads

```cpp
namespace strij::loaders {

// Selects Registry<GatewaySchedulerFactory> via the context argument type.
auto CreateScheduler(const config::ExtensionConfig& config,
                     extensions::GatewayFactoryContext& context)
    -> absl::StatusOr<extensions::SchedulerPtr>;

// Selects Registry<NodeSchedulerFactory> via the deps argument type.
auto CreateScheduler(const config::ExtensionConfig& config,
                     const nodeagent::NodeSchedulerDeps& deps)
    -> absl::StatusOr<extensions::SchedulerPtr>;

} // namespace strij::loaders
```

The side is already encoded in the dependency/context object every call site must hold, so overload resolution is the most direct expression of "given this side's config and this side's dependencies, produce a `Scheduler`." The factory interface never appears at the call site. `GatewayFactoryContext` and `NodeSchedulerDeps` are unrelated (both delete copy/move, no converting constructors), so the overload set is unambiguous.

Both overloads share the existing private template, kept in the `.cc`:

```cpp
namespace strij::loaders {
namespace {
template <typename FactoryT, typename ContextT>
auto createSchedulerFromExtension(const config::ExtensionConfig& ext, ContextT& context)
    -> absl::StatusOr<extensions::SchedulerPtr> { /* unchanged body */ }
} // namespace

auto CreateScheduler(const config::ExtensionConfig& config,
                     extensions::GatewayFactoryContext& context)
    -> absl::StatusOr<extensions::SchedulerPtr> {
  return createSchedulerFromExtension<gateway::GatewaySchedulerFactory>(config, context);
}

auto CreateScheduler(const config::ExtensionConfig& config,
                     const nodeagent::NodeSchedulerDeps& deps)
    -> absl::StatusOr<extensions::SchedulerPtr> {
  return createSchedulerFromExtension<nodeagent::NodeSchedulerFactory>(config, deps);
}
} // namespace strij::loaders
```

Alternatives considered (see the exploration that produced this change):

- **A. Keep two names**, `CreateGatewayScheduler` / `CreateNodeScheduler`, just moved into `strij::loaders`. Smallest diff and best greppability, but preserves the duplication the request was about.
- **C. One explicit template**, `CreateScheduler<FactoryT>(config, context)`. Requires the header to expose the template body and `Registry`/`Any`, and leaks the factory class into every call site — louder than the two wrappers it replaces. Fights the repo's "non-template public function, impl in `.cc`" convention.
- **D. One deduced template via a `SchedulerFactoryOf<ContextT>` trait.** Replaces two wrappers with two trait specializations and puts machinery in the header; no net reduction.

### D3. Private template stays in the `.cc`

`createSchedulerFromExtension` becomes an anonymous-namespace helper in `strij::loaders` (it was `strij::extensions`). Keeping it in the `.cc` preserves the current property that the loader's public header exposes only the two concrete overloads, not the generic machinery.

### D4. Test placement and the allowlist

`test/common/extensions/scheduler_factory_test.cc` opens `strij::gateway::schedulers` because it needs gateway impls (`NodeDirectoryImpl`, `ResultReceiverStorageImpl`) to build a `MockGatewayFactoryContext`. Moving it to `test/common/loaders/scheduler_factory_test.cc` keeps it under a common/ tree that opens a side namespace, so it remains a genuine cross-cutting test. Decision: move the file to `test/common/loaders/` (mirroring the loader) and move its allowlist entry to the new path, with the comment updated to name `strij::loaders::CreateScheduler`. Splitting per-side tests is deferred — the single shared-behavior test (unknown/empty name rejection, typed-config tolerance, null rejection) is intentionally side-agnostic and reusing gateway impls is cheaper than duplicating fixtures.

`CROSS_CUTTING_ALLOWLIST` changes:

```
- "src/common/extensions/scheduler.hh",       # stale: file long gone
- "src/common/extensions/scheduler.cc",       # file removed by this change
- "src/common/extensions/scheduler_loader.hh",# file removed by this change
- "test/common/extensions/scheduler_factory_test.cc",
+ "test/common/loaders/scheduler_factory_test.cc",
```

`factory_context.hh` stays (it forward-declares only).

### D5. Consumers and build targets

`//src/common/extensions:scheduler_loader_lib` → `//src/common/loaders:scheduler_loader_lib`, with the dep string updated in `src/gateway/core/scheduler_router/BUILD.bazel`, `src/nodeagent/core/BUILD.bazel`, and `test/common/extensions/BUILD.bazel` (test moved to `test/common/loaders/BUILD.bazel`). Call sites qualify: `loaders::CreateScheduler(...)` in `scheduler_router.cc` and `nodeagent_scheduler_router.cc`. The `scheduler_loader_lib` target in `src/common/loaders/BUILD.bazel` keeps `visibility = ["//visibility:public"]` and its deps unchanged, **including `//include/strij/extensions:factory_context_interface`**: the loader header names `extensions::GatewayFactoryContext` directly, so include-what-you-use keeps both the direct include and the direct dep even though `strij/extensions/scheduler.hh` also pulls the header in transitively.

### D6. Deferred: shared `ResolveExtensionConfig<FactoryT>` helper

`evaluator_loader.cc` and `scheduler_loader.cc` duplicate the "registry lookup by name → `NotFound` with registered names → `CreateEmptyConfigProto` → tolerate-packing of `typed_config` → unpack" preamble. A shared `template <typename FactoryT> ResolveExtensionConfig(const config::ExtensionConfig&, std::string_view kind) -> absl::StatusOr<std::unique_ptr<google::protobuf::Message>>` in `strij::loaders` would unify the error-prone half while leaving each category's create/compile step (and its distinct null semantics) local. With exactly two loaders the duplication is ~20 lines, so this is **not** done now; a TODO task (below) tracks revisiting it when more loaders appear (the `kind` parameter exists because each category names itself in the error string).

## Risks / Trade-offs

- **[Overloaded name reduces greppability]** `CreateScheduler` no longer names the side at a glance → Mitigation: the argument type names it, and the two overload definitions live adjacently in one small `.cc`; `AGENTS.md` documents the name.
- **[Missing a call site / dep string]** a stale `common/extensions/scheduler_loader.hh` include or `//src/common/extensions:scheduler_loader_lib` dep fails the build immediately (headers/targets vanish) → Mitigation: `make build` + `make test` cover the whole graph; the `rg` sweep is explicit in tasks.
- **[Test still cross-cutting]** moving the test does not remove its allowlist entry → Mitigation: documented in D4; the loader's own namespace bleed is what is removed.
- **[Empty parent package]** deleting `src/common/extensions/BUILD.bazel` while `evaluators/BUILD.bazel` remains is valid Bazel (package boundary is the subdir) → Mitigation: confirm with `make build`.

## Migration Plan

Single atomic commit — a partial move does not compile (target/header references break), which is acceptable for an internal refactor. Rollback = revert the commit; no proto, lock-file, config, or wire-format state.

## Open Questions

- Whether the cross-cutting scheduler-loader test should eventually split into `test/gateway` and `test/nodeagent` slices (removing the last allowlist entry) — deferred; the shared behavior is currently tested once by design.
