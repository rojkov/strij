# Proposal

## Why

The `extension-api-surface` requirement now places shared config→instance bridge glue under `src/common/loaders/` in the bare `strij::loaders` namespace. The jq evaluator change introduced that layer with `CreateEvaluator` but deliberately deferred the pre-existing scheduler loader (`src/common/extensions/scheduler_loader.hh`, `src/common/extensions/scheduler.cc`). The tree therefore carries two conventions for the same concern, and the scheduler loader's side namespaces (`strij::gateway` / `strij::nodeagent`) remain on the cross-cutting allowlist of `tools/check_namespace_coherence.py`. This change completes the migration and consolidates the two scheduler-loader entry points into a single `strij::loaders::CreateScheduler` overload set.

## What Changes

- Move scheduler-loader glue out of `src/common/extensions/` into `src/common/loaders/`:
  - `src/common/extensions/scheduler_loader.hh` → `src/common/loaders/scheduler_loader.hh`
  - `src/common/extensions/scheduler.cc` → `src/common/loaders/scheduler_loader.cc` (renamed — the file is pure loader glue; the `Scheduler` contract itself already lives in `include/strij/extensions/scheduler.hh`).
- Collapse `strij::gateway::CreateGatewayScheduler` and `strij::nodeagent::CreateNodeScheduler` into one overloaded name in the bare `strij::loaders` namespace:
  - `strij::loaders::CreateScheduler(const config::ExtensionConfig&, extensions::GatewayFactoryContext&)`
  - `strij::loaders::CreateScheduler(const config::ExtensionConfig&, const nodeagent::NodeSchedulerDeps&)`

  The side's factory registry (`Registry<GatewaySchedulerFactory>` vs `Registry<NodeSchedulerFactory>`) is selected by overload resolution on the dependency/context argument; the shared `createSchedulerFromExtension` template stays private to the `.cc`.
- Move Bazel target `scheduler_loader_lib` from `//src/common/extensions` to `//src/common/loaders`; update the three consumers (`src/gateway/core/scheduler_router`, `src/nodeagent/core`, `test/common/extensions`).
- Update include paths (`common/extensions/scheduler_loader.hh` → `common/loaders/scheduler_loader.hh`) and qualify call sites with `loaders::`.
- Retire the now-obsolete `CROSS_CUTTING_ALLOWLIST` entries in `tools/check_namespace_coherence.py` for the moved loader (`scheduler_loader.hh`, `scheduler.cc`) and the stale `scheduler.hh`; the cross-cutting test entry is revisited by placement (see design).
- Update the `AGENTS.md` references from `src/common/extensions/scheduler_loader` to `src/common/loaders/scheduler_loader`.
- **BREAKING**: none to external behavior — loaders are `src/`-internal and no public `include/` header changes.
- No behavior change: identical registry lookup, `typed_config` tolerance, null-rejection, and error semantics.

## Capabilities

<!-- Pure layout/refactor conformance: the governing requirement already exists in
     openspec/specs/extension-api-surface/ ("Shared loader glue has a home",
     "Loader glue has a namespace"). No requirement text changes, so this change
     sets skip_specs: true and declares no capability deltas. -->

### New Capabilities

None.

### Modified Capabilities

None — the spec already mandates the target layout and namespace; this change brings the code into conformance with it.

## Impact

- **Code**: removes `src/common/extensions/{scheduler.cc,scheduler_loader.hh}`; adds `src/common/loaders/scheduler_loader.{cc,hh}`. Call sites: `src/gateway/core/scheduler_router/scheduler_router.cc`, `src/nodeagent/core/nodeagent_scheduler_router.cc`, and test `test/common/extensions/scheduler_factory_test.cc`.
- **Build**: `//src/common/extensions:scheduler_loader_lib` → `//src/common/loaders:scheduler_loader_lib`; deps updated in the gateway scheduler-router BUILD, `src/nodeagent/core/BUILD.bazel`, and the test BUILD. `src/common/extensions/BUILD.bazel` loses its last target (only `evaluators/` remains).
- **Tooling**: `tools/check_namespace_coherence.py` allowlist shrinks; `tools/check_include_purity.py` unaffected.
- **Docs**: `AGENTS.md` layout and scheduler bullets; `docs/extension-author-guide.md` already describes `strij::loaders` generically and needs no change.
- **Untouched**: no proto, no public `include/` surface, no `test/consumer` change.
