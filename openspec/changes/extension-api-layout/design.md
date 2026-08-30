# Design: Extension API Layout

## Context

Strij targets third-party developers building private extensions (schedulers, node discovery, task handlers) in separate repositories, consuming Strij like Envoy is consumed as a library — compile-time registration, `alwayslink`'d into a recomposed executable. Current state:

- The extension model already works internally: `REGISTER_FACTORY` static-init macros into `Registry<FactoryInterface>` singletons, `alwayslink = True` extension libraries, and config-driven runtime selection (`gateway.cc` looks up configured names in `Registry<...>`; no hardcoded factory knowledge).
- The seed of the desired split exists: `include/strij/event/*` + `include/strij/common/pure.hh` are already `//visibility:public`, and `strij_include_prefix()` already maps any package under `include/` or `src/` to a clean include prefix — so relocating files costs nothing in the build system.
- The Envoy `Dispatcher`/`DispatcherImpl` idiom (abstract contract in public headers, impl hidden in source) is already the house style for `Dispatcher`, `ProtocolParser`, and the factory contexts.
- Three blockers for real third-party consumption: (a) `MODULE.bazel` has no `module()` declaration (no `bazel_dep` possible), (b) the executables are `cc_binary` targets with no reusable library seam and all composition hardcoded in `src/exe/*/BUILD.bazel`, (c) `GatewayFactoryContextImpl`/`NodeagentFactoryContextImpl` are compiled inline into the binaries, so framework logic is neither separable nor unit-testable.
- Namespaces today do not express ownership cleanly: everything extension-related is `strij::extensions::*` with impls nested as `strij::extensions::schedulers::*`, while gateway and nodeagent sides are mixed in `src/extensions/schedulers/` (shared `Scheduler` + two factory types + loaders).

Stakeholders: extension authors (primary), maintainers of the bundled extensions, binary/framework maintainers.

## Goals / Non-Goals

**Goals:**
- Establish a documented, Bazel-enforced public API boundary: `include/strij/` is the only surface external repos may depend on.
- Side-first mental map (common / gateway / nodeagent) so a developer knows where a piece belongs without thought.
- Abstract-contract rule: consumed types leak into the public surface only as `PURE` interfaces; concrete `*Impl` stays private.
- Consumability spine: `module(name = "strij")`, framework libraries, exported composition macros for building custom gateway/nodeagent binaries with private extensions.
- Document the namespace doctrine.
- Preserve all current behavior; the change is structural (reorg + names + seams), not behavioral.

**Non-Goals:**
- No dynamic loading (`dlopen`/`.so` plugins) — explicitly deferred; static alwayslink + one binary remains the model. If this is ever revisited, protobuf/`std::` types across the `.so` boundary become the design driver.
- No change to the wire protocol, TLV framing, scheduler routing semantics, config formats, or registration API (`REGISTER_FACTORY`, `Registry<T>` signatures).
- Not archiving existing capability specs; their requirements are behavior-preserving.
- Not a 1.0 API stability promise — only the boundary mechanics are introduced here.

## Decisions

### D1: Side-first tree

```
include/strij/                         src/                              api/            test/
  common/           (event, extensions)   common/{core,extensions}       api/common       test/common
  gateway/            (extensions)        gateway/{core,extensions,exe}  api/gateway      test/gateway
  nodeagent/ (extensions)                nodeagent/{core,extensions,exe} api/nodeagent    test/nodeagent
```

- **Rationale**: encodes "extension category → side" (round_robin/capability_aware/static node discovery ↔ gateway; push/echo/piped_executable ↔ nodeagent), enables a cross-side dependency guard (gateway code may not depend on nodeagent code and vice versa, verifiable by visibility), and gives each side a complete story.
- **Alternatives considered**: (a) function-first (`src/{core,extensions}/{gateway,nodeagent,common}`) — close to today, keeps interfaces in one place, but cannot enforce side isolation and buries the per-side mind map; (b) keep current tree + visibility only — cheapest, but abandons the mental model entirely. Rejected both.
- **Details**: shared `Scheduler` contract, `Registry`, base `FactoryContext`, and the event/io/logging/config layers live under `common/`. The exe `main()` lives per side (`src/gateway/exe/gateway.cc`, `src/nodeagent/exe/nodeagent.cc`), mirroring where a side's binary belongs.

### D2: Curated `include/` with the abstract-contract rule

- Only the *seam contracts* (implement/extend/consume interfaces, value types crossing signatures) go public. Leaked consumed services are made `PURE` abstract contracts (`NodeDirectory`, `ResultReceiverStorage`, `AdmissionController`, `RunTaskService`, `FunctionResolver`; `Logger` is dropped from the context instead — see D4) with `*Impl` classes in `src/`.
- **Rule**: freezing an interface is cheap (PURE + docs); freezing a concrete type is prohibited.
- **Alternatives considered**: full mirror (every public header physically in `include/`, 1:1) — cleanest doctrine but a large permanent freeze surface; rejected as over-committal for pre-1.0.
- **`io::Connection` exclusion**: stays concrete and public (abstracting a leaf shell that owns an fd is invasive). Tracked by a TODO to narrow `Scheduler::HandleFrame` (e.g. pass a slimmer facade) so the exclusion can later be removed.

### D3: Namespace doctrine

- Shared concerns: bare concern namespaces (`strij::event`, `strij::io`, `strij::logging`, `strij::config`, `strij::task`).
- Shared extension scaffolding: `strij::extensions` (`Registry`, `Scheduler`, base `FactoryContext`).
- Side-owned: `strij::gateway` / `strij::nodeagent`, optionally with a category sub-namespace for impls (`strij::gateway::schedulers::RoundRobinSchedulerFactory`).
- No literal `strij::common` namespace.
- **Rationale**: matches how the code already namespaces (grep shows `strij::io`, `strij::gateway`, `strij::nodeagent` in use), keeps locatability near-perfect, avoids `strij::common::*` verbosity. The `::extensions` marker survives on cross-cutting plugin scaffolding so "this is a plugin point" is a *namespace-level* signal.
- **Alternatives considered**: literal mirroring (`strij::common::extensions::..., strij::gateway::core::...`) — perfect mapping, noisy, introduces `strij::common`; flat side-only namespaces — loses the plugin-point signal. Caveat: `include_prefix` already decouples include path from physical path, so namespace↔path is a *convention* (enforced by review checklist, not the compiler).

### D4: Consumability spine

- Add `module(name = "strij", version = ...)` to `MODULE.bazel` (external repos consume via `bazel_dep` + `local_path_override`/`git_override`).
- Split each exe into a framework `cc_library` (`RunGateway` / `RunNodeagent`, with the side's concrete factory-context impl) + a thin `main()`. Framework logic becomes unit-testable.
- Export `strij_gateway_binary` / `strij_nodeagent_binary` `.bzl` macros: `(name, extensions = [...], visibility = ...)`. The macro expands to a `cc_binary` whose deps = framework lib + alwayslink extension targets + abseil/protobuf boilerplate, mirroring today's `src/exe/*/BUILD.bazel`.
- **`Logger()` leaves `FactoryContext`**: decided to drop `Logger()` from the abstract `FactoryContext` (resolving the pre-existing "TODO: is Logger() really needed?"). No bundled extension requires it, so `logging::Logger` stays fully private and needs no interface-ization. Existing logging integration points (thread registration, explicit `Logger::instance()` use) are unchanged. Task 4.1 performs the removal.
- **Alternatives considered**: (a) `dlopen` plugins — rejected (see Non-Goals); (b) fork-and-recompose (document "edit `src/exe/*/BUILD.bazel`") — rejected, external repos must not touch the tree (spec: Extension author workflow).

### D5: Extension authors need their exc configuration protos too

- External extensions carry their own `typed_config`; the macro contract includes their generated proto deps. Bundled extensions (`push`, `round_robin`, `echo`, `piped_executable`, `static_node_discovery`) demonstrate the pattern and serve as reference implementations.

### D6: Exact mirroring of `api/` and `test/` per side

- `api/` mirrors the sides exactly. Side runtime schemas: `api/gateway/config/gateway.proto` (generated `gateway_cc_proto`), `api/nodeagent/config/nodeagent.proto`. Shared config scaffolding stays in `api/common/config/` (`extensions.proto`, `options.proto`), shared task/node schemas in `api/common/task/` and `api/common/node/`. Extension config protos sit beside their extension: `api/gateway/extensions/schedulers/{round_robin,capability_aware}`, `api/gateway/extensions/node_discovery/static`, `api/nodeagent/extensions/schedulers/push`, `api/nodeagent/extensions/task_handlers/{echo,piped_executable}`.
- `test/` mirrors `src/` per side (`test/common`, `test/gateway`, `test/nodeagent`) with the existing auto-visibility mechanism following the move.

## Sequence: private extension → composed binary → runtime

```
 extension author repo                     strij module
 ─────────────────────                     ────────────
  1. implements side interface (include/strij/gateway/extensions/...)
  2. registers via REGISTER_FACTORY in .cc
  3. cc_library(..., alwayslink = True, deps = public strij targets)
  4. strij_gateway_binary(name, extensions = [<their lib>])
       │  macro expands to
       ▼
  cc_binary( gateway framework_lib + alwayslink(deps) + protos + abseil )
  start of binary:
    main() → RunGateway(cfg-json/yml/flags)
        → builds GatewayFactoryContextImpl
        → for each name in config.schedulers():
              Registry<strij::gateway::SchedulerFactory>.GetFactory(name)
                                  └─ found? the author's alwayslink'd factory
              Create(typed_config, context)  → SchedulerPtr
        → SchedulerRouter serves /tasks/{type}
```

Runtime selection stays purely config-driven — the author's extension participates exactly like the bundled ones, with zero strij edits.

## Risks / Trade-offs

- **Common/ grab-bag entropy** (`common/extensions`, `common/core` absorb side-specific logic) → admission rule: "does this *need* to be shared by both sides?" answered in review; side-local duplications are preferred over premature sharing; new `common/` additions require explicit justification.
- **Public surface creep via transitive includes** (an external header referencing a `src/` header breaks the doctrine) → abstract-contract rule + a repo check (grep that `include/` reference only `include/`, `absl`, protobuf, std headers) in CI.
- **Namespace↔path drift** → namespace carries side/concern, dot exactly as directory; add to `clang-tidy`/review checklist; a simple grep-based coherence check.
- **Mechanical churn breaks everything at once** → single-owner reorg; do it as one atomic migration verified by `make test` + `make build` + `make clang-tidy`; renames only, no logic changes, so bisection is trivial.
- **Public interface-ization forces internal refactor of facade objects** (NodeDirectory, ResultReceiverStorage, etc. become abstract + Impl) → bounded worklist (enumerated in D2/D4); each is a mechanical extract-interface step with existing tests as safety net.
- **Module consumability unproven** → after the spine lands, add a smoke-test consumer repo (or `//test:consumer` harness) doing `bazel_dep("strij")` + `local_path_override` + one alwayslink extension, to lock in the workflow end-to-end.

## Migration Plan

1. **Spine first** (small, de-risks everything): `module()` declaration; extract framework libs + factory-context impls from `src/exe/`; export composition macros; shrink `main()`s. Verify `make test`.
2. **Side-first reorg** of `src/`, `include/`, `api/`, `test/` (`common|gateway|nodeagent`): pure moves + BUILD target renames + include-line updates. Verify `make build && make test`.
3. **Namespace restructure** per D3 across extension + side-owned code (behavior-neutral renames).
4. **Interface-ization** of leaked consumed types per D2 (abstract + `*Impl`) and drop `Logger()` from `FactoryContext` per D4; add the `Connection` seam-narrowing TODO.
5. **Write the Extension Author's Guide** (AGENTS.md + docs): layout map, namespace doctrine, how to build a private extension (interface → factory → alwayslink → macro), the visibility/stability promise.
6. **CI checks**: include/-purity grep, namespace coherence check; smoke-test consumer for the composition workflow.

## Open Questions

- Whether the composition macros wrap `strij_cc_library` (so extension repos inherit include-prefix/visibility defaults) — lean yes, confirm in step 1.