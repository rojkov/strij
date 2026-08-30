# Proposal: Extension API Layout

## Why

Future developers will want to build their own private extensions (schedulers, node discovery, task handlers) in their own repositories, depending on Strij like Envoy is used as a library: compile-time registered, alwayslink'd into a recomposed binary. Today nothing makes this possible — there is no `module()` declaration (an external repo cannot `bazel_dep` Strij), no composition seam (the executables are `cc_binary` targets that cannot be reused), and the public/private boundary is undefined: side-specific extension code (gateway `round_robin`/`capability_aware`, nodeagent `push`) shares folders, interfaces and implementations intermingle, and namespaces do not reflect ownership. Pre-1.0 is the time to draw this boundary cheaply.

## What Changes

- **BREAKING**: Restructure the tree side-first. First level: public (`include/strij/`) vs private (`src/`). Second level: sides (`common/`, `gateway/`, `nodeagent/`). Third level (in `src/` only): `core/` vs `extensions/`.
- **BREAKING**: Relocate extension interfaces, factories, and bundled implementations into side-owned packages. The shared `Scheduler` contract stays in `common`; `GatewaySchedulerFactory` moves to the gateway side, `NodeSchedulerFactory` and task handlers to the nodeagent side.
- Introduce the abstract-contract rule: anything an extension author *consumes* through factory contexts or interface signatures is a pure-abstract (`PURE`) contract living in `include/`; concrete `*Impl` classes stay in `src/`. `io::Connection` is an accepted exclusion (concrete + public); a TODO tracks narrowing the `Scheduler::HandleFrame` seam off `Connection&`.
- **BREAKING**: Adopt and document a namespace doctrine mirroring side/concern ownership (shared concerns bare: `strij::event`, `strij::io`, `strij::extensions`; side-owned under `strij::gateway` / `strij::nodeagent`; no literal `::common::`).
- Enable consumability: add `module(name = "strij", ...)` to `MODULE.bazel`, split exe logic into testable framework libraries (`RunGateway` / `RunNodeagent`), move the factory-context `*Impl` classes into the framework, and export `strij_gateway_binary` / `strij_nodeagent_binary` composition macros that link framework + alwayslink extension targets.
- Mirror `api/` and `test/` by side (`api/common|gateway|nodeagent`, `test/common|gateway|nodeagent`).
- Enforce the boundary with existing Bazel visibility mechanics: everything under `src/` stays `//visibility:private` (+ auto test visibility), only `include/` targets are `//visibility:public` — the only targets external repos may depend on.

## Capabilities

### New Capabilities

- `extension-api-surface`: the public API contract for third-party extension authors — `include/strij/` layout by side, the abstract-contract (`*Impl`) rule including the `Connection` exclusion, the namespace doctrine, the alwayslink registration + composition macro workflow, and the Bazel visibility/consumability spine.

### Modified Capabilities

None. Existing capability requirements (scheduler routing, registration, node discovery, task handlers, TLV protocol) are behavior-preserving; the change affects physical layout, namespaces, and include paths, which are documented by the new capability.

## Impact

- **Every package and target under `src/`, `include/`, `api/`, `test/`**: moved/renamed (mechanical, no logic change).
- **Namespaces** across all extension code (`strij::extensions::...` splits into `strij::extensions` shared scaffolding + side-owned namespaces).
- **Interfaces**: leaked consumed types (`NodeDirectory`, `ResultReceiverStorage`, `AdmissionController`, `RunTaskService`, `FunctionResolver`) split into abstract contract + `*Impl`; `Logger()` dropped from the context (fully private, no interface needed); `GatewayFactoryContextImpl`/`NodeagentFactoryContextImpl` move out of `src/exe/`.
- **Build system**: `MODULE.bazel` (`module()` declaration), new exported `.bzl` files, framework library targets, per-side executable targets.
- **The two executables**: become thin `main()` wrappers over framework libraries.