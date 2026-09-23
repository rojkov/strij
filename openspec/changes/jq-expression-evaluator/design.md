# Design

## Context

See proposal.md - Why. The `open_workflow` task handler (change 2) needs a runtime expression engine whose reference semantics are jq's. Strij today has no expression engine, no third-party C dependency that is a "compiler library" (liburing/llhttp/yaml_cpp are I/O/config, and all three are `include/`-clean), and a hard include-purity rule: headers under `include/` may reference only `include/`, absl, protobuf, and std. libjq's public headers (`jq.h`, `jv.h`) therefore force the evaluator to live under `src/` — the shared `extensions::Registry`/`REGISTER_FACTORY` machinery already exists and is header-only (`include/strij/extensions/extension_registry.hh`), so a shared, internal, categorized extension is the natural home. The spec (see specs/expression-evaluation) defines the observable contract this design realizes.

## Goals / Non-Goals

**Goals:**

- Embed jq 1.8.2 as a first-class Bazel dep whose artifacts (libjq.a + `jq.h`/`jv.h`) other `src/` targets can consume.
- One RAII value type for the boundary (refcount-safe `jv`), so no consumer ever calls `jv_free`-style primitives.
- A shared internal `Evaluator` extension category: factory + deps bundle + Registry registration, mirroring the scheduler/fetcher/handler categories and the `strij::extensions` namespace marker.
- A both-sides loader in a new cleanly-named `src/common/loaders/` layer.
- Every behavior contract in specs/expression-evaluation covered by GoogleTest unit tests; all repo checks green.

**Non-Goals:**

- `open_workflow` / per-document language selection / `${ }` scanning / strict-loose modes (change 2 scope).
- Moving `scheduler_loader` into the new loaders layout (explicit follow-up; the allowlist entry it needs is out of scope).
- Public `include/` promotion of `Evaluator` (requires resolving the include-purity exception for libjq headers; rework when an external extension consumes it).
- Generator policy (single-output-vs-array for jq multi-value runs) — resolved in change 2.
- Timeouts / worker-thread offloading for long-running expressions.

## Decisions

### D1. libjq via `configure_make` at the repo root, following the liburing pattern

`http_archive("jq", ...)` in `MODULE.bazel` (jq 1.8.2, sha256 `71b8d6e8f5fe81f6c6d0d110e3892251f6ce76ed095abd315e26e6e1193af3af`, verified by the spike against the published `sha256sum.txt`), using the **release-dance tarball** `jq-1.8.2.tar.gz` — not the `archive/refs/tags` source archive, which ships no generated `configure` and would force autoreconf (autoconf/automake absent from the build sandbox). The release tarball includes the autotools output (`configure`, `parser.c`, `lexer.c`) and the vendored oniguruma. Build via `configure_make(name = "libjq", configure_in_place = True, out_static_libs = ["libjq.a"], lib_source = "@jq//:all_srcs")` in root `BUILD.bazel` (the `//:liburing` shape — see `src/common/core/event/BUILD.bazel` line 9 for consumption). Configure flags: `--disable-maintainer-mode` (skip any regeneration) and `--with-oniguruma=builtin` (use the oniguruma vendored in-tree instead of a system dep). Consumers include `#include "jq.h"` / `#include "jv.h"` exactly like the existing `#include "liburing.h"`.

Alternatives considered: **shelling out to a `jq` binary** per expression (per-eval exec cost, no clean named-variable binding) — rejected; **vendoring the jq amalgamation** (none exists for 1.8) — rejected; **dynamic linking** — rejected, the tree is static-web-of-deps and `out_static_libs` is the established pattern.

### D2. One RAII `Jv` type, internal to `src/`

`strij::utils::Jv` at `src/common/core/utils/jv.hh` (package prefix `common/core/utils`, next to the existing utils). Move-only wrapper: owns one `jv`, `jv_copy`s on copy where libjq needs it, `jv_free`s on destruction (its `jv`-destructor semantics are the wrapper's job, never the consumer's). Statics/helpers for the constructor-from-JSON-text (`jv_parse`) and render-back-to-JSON-text (`jv_dumpf`) cases. Lives in `strij::utils` because it is a shared value concern, not an extension.

Alternatives: **raw `jv` everywhere with manual refcount calls** — error-prone, exactly what the RAII wrapper exists to prevent; **a second value type (e.g. `boost::json`)** — not jq compatible at the boundary.

### D3. Evaluator category is internal to `src/` (for now)

`Evaluator`/`EvaluatorFactory`/`EvaluatorDeps`/`EvaluatorPtr` live at `src/common/extensions/evaluators/evaluator.hh` under `strij::extensions::evaluators` (categorized shared extension, following the `strij::gateway::schedulers` naming pattern; outer `strij::extensions` = the plugin-point marker per `extension-api-surface`). The category stays internal because `include/` may not reference libjq headers — no `include/` header can name `Jv` or its factory until we decide the purity exemption. The `extension_registry_lib` target (`include/strij/extensions/extensions.../extension_registry.hh`) is already `//visibility:public`, so the internal evaluator `.cc` files can register into `Registry<EvaluatorFactory>` without any visibility change.

Alternatives: **public `include/strij/extensions/evaluator.hh`** — would force a JSON pass-through ABI (strings in / strings out) to dodge `jv.h` in `include/`; loses the value-boundary contract and the direct `vector<Jv>` outputs. Not worth it until a genuinely external extension appears.

### D4. `Evaluator` == one compiled program; `EvaluatorFactory::Compile` creates it

```
EvaluatorFactory (abstract, Rule-of-Five-deleted, mirrors gateway/nodeagent factory shape)
  Name() -> std::string
  CreateEmptyConfigProto() -> MessagePtr
  Compile(const google::protobuf::Message& config,
          std::string_view source,
          std::vector<std::string> variable_names,
          const EvaluatorDeps& deps) -> absl::StatusOr<EvaluatorPtr>

Evaluator (abstract)
  Run(const Jv& input, std::span<const Jv> args) -> absl::StatusOr<std::vector<Jv>>
```

`args` is positionally aligned with `variable_names` from `Compile`. libjq mapping (verified in the 1.1 spike against `src/execute.c` and `src/jq_test.c`): `jq_compile_args` accepts `{name: value, ...}` (or `[{name, value}, ...]`) and **bakes the values in at compile time** — there is no per-run rebind in libjq. So `Compile` performs a validation compile (null placeholders bound to `variable_names`, so syntax errors surface early with `jq_get_error_message`) and captures the source + variable order, while each `Run` re-compiles with `jq_compile_args` (name/value pairs built from the positional args), then `jq_start`/`jq_next` collects all outputs into `vector<Jv>`. Recompile-per-`Run` is cheap for open_workflow-sized programs and keeps the `Evaluator` object immutable, so one compiled handle is safe to share across threads. The change-2 memo-cache idea still holds (memo per `(language, expression)`), but it caches source+varnames, not bytecode. **All** outputs are returned; generator policy is a change-2 concern. `EvaluatorDeps` is an empty bundle for v1 (jq needs nothing from the framework) — passing `const&` keeps the factory signature uniform with `NodeSchedulerDeps`/`DataDependencyFetcherDeps` and leaves room to grow (e.g. a dispatcher ref) without breaking callers.

Alternatives: **two-type design** (a per-document compiler object that produces per-expression programs) — rejected for now because nothing in libjq shares state across programs; a per-document memo map of expression-text → `Evaluator` is the cheaper form of the same thing and belongs entirely to `open_workflow` (change 2).

### D5. Loader layer: `strij::loaders::CreateEvaluator`, new `src/common/loaders/`

`src/common/loaders/evaluator_loader.{hh,cc}` (prefix `common/loaders`) declares `strij::loaders::CreateEvaluator(const config::ExtensionConfig&, const EvaluatorDeps&, std::string_view source, std::vector<std::string> variable_names) -> absl::StatusOr<EvaluatorPtr>`. It mirrors `CreateGatewayScheduler`'s shape (config → instance): resolve `config.name()` in `Registry<EvaluatorFactory>`, unpack (or tolerate the absence of) `typed_config`, forward to `Compile`, return `NotFound` for unregistered names. Two reasons this earns its own layer: it is shared by both sides and is *config→instance glue*, which is exactly what `src/common/extensions/` is not for (that dir is for pluggable extension implementations). The new `strij::loaders` bare concern namespace is documented alongside `strij::event`/`strij::io` etc. (`extension-api-surface` delta). The scheduler loader is deliberately left untouched — moving it here is a follow-up that also resolves its cross-cutting allowlist entry.

Alternatives: **inline `Registry<EvaluatorFactory>::instance().GetFactory()` at the call site** — fine for one call site, but open_workflow will call it per expression and the config→factory resolution is the loader contract; **shoving it into `src/common/extensions/`** — hides the config-glue concern and mislabels it as an extension. Both rejected.

### D6. Naming and packaging orthogonality

- jq impl + factory: `src/common/extensions/evaluators/jq/jq_evaluator.{hh,cc}` (prefix `common/extensions/evaluators/jq`, mirroring the config proto package `api/common/extensions/evaluators/jq/`), namespace `strij::extensions::evaluators::JqEvaluator` + `JqEvaluatorFactory`. Registered via `REGISTER_FACTORY`/`REGISTER_FACTORY_FULLY_QUALIFIED` in the `.cc`; `alwayslink = True` on the library so the static registration survives link-time GC once a binary links it.
- Config proto: `api/common/extensions/evaluators/jq/jq.proto`, `package strij.extensions.evaluators.jq`, message `JqEvaluatorConfig` (empty fields for v1 — `CreateEmptyConfigProto` must return something unpackable and round-trippable, mirroring how the scheduler factories handle absence of `typed_config`).
- Include prefixes are auto-derived from package path by `build_system.bzl`, so consumers write `"common/extensions/evaluators/jq/jq_evaluator.hh"`, `"common/core/utils/jv.hh"`, `"common/loaders/evaluator_loader.hh"` (matching existing prefix rules, not filesystem roots).
- Both `tools/check_namespace_coherence.py` and `tools/check_include_purity.py` need **no** edits: all new `.hh`/`.cc` open only `strij::extensions(...)`, `strij::loaders`, `strij::utils`, `strij::config`, and nothing under `include/` is touched.

### D7. `test/consumer` stays untouched

The jq evaluator is a bundled extension linked only by its own tests and, later, the in-tree `nodeagent` binary's extension list (`//src/nodeagent/exe`). `strij_nodeagent_binary` composes *caller-supplied* extensions over the alwayslink framework (`nodeagent_framework` deps the category interfaces, not the jq impl), so neither the consumer binaries nor any `@strij` target they reach transitively depends on `//:libjq` — meaning consumers do not need to redeclare the jq `http_archive` (unlike liburing/llhttp/yaml_cpp, which the framework links). When `open_workflow` links the evaluator into the *default* nodeagent binary, consumers who replace the task-handler set are still unaffected.

## Risks / Trade-offs

- **[jq's static lib has transitive deps]** jq links oniguruma (builtin) and decNumber; whether they surface as `libonig.a`/analyzed-generated artifacts or are folded into `libjq.a` must be verified → **Mitigation**: a dedicated spike task (task 1.1) that builds `//:libjq` and links a probe program before any real code lands; if oniguruma/decNumber appear as separate archives, add them to `out_static_libs`/links.
- **[`configure_make` header exposure]** require `jq.h`/`jv.h` reachable as bare includes the way `liburing.h` is today → **Mitigation**: probe includes in the same spike; rules_foreign_cc exposes the install include dir for `configure_make`, the liburing precedent holds.
- **[No CPU-bounding]** libjq has no instruction budget; a hostile-but-valid program spins the event-loop thread → **Mitigation**: documented v1 limitation (spec + AGENTS.md); the design keeps `Run` behind the `Evaluator` seam so a future bounded-worker implementation swaps in without changing consumers.
- **[Unpacking an absent `typed_config`]** proto3 `Any` behavior for an empty `JqEvaluatorConfig` → **Mitigation**: loader treats missing `typed_config` as an empty message (the exact tolerance `scheduler_loader` already has); covered by a loader test.
- **[Autotools regeneration]** if the tarball's generated sources are stale, `configure` may attempt to re-run automake (requires bison/flex in CI) → **Mitigation**: `--disable-maintainer-mode` and the spike build; pinned sha256 means this either works for the version or fails immediately at the spike with no code on top.

## Migration Plan

None — purely additive (new Bazel target, new `src/`/`api/` packages, docs). Rollback = revert the three new source areas; `scheduler_loader` and all existing consumers are untouched. `module`/`http_archive` changes are root-module-only and carry no lock-file migration (bzlmod resolves `jq` on next build; `MODULE.bazel.lock` regenerates).

## Open Questions

- Whether `open_workflow` memoizes compiled `Evaluator`s per `(language, expression)` within a document — a change-2 efficiency choice, does not affect this change.
- Whether jq evaluator config should later carry resource/timeout hints the handler can enforce — deferred; `JqEvaluatorConfig` starts empty.
- Whether `EvaluatorDeps` stays empty or gains a `Dispatcher&` — deferred until the first eval category needs async; the `const&` signature future-proofs it.
- When to promote `Evaluator` to `include/strij/extensions/` and how to legalize libjq headers for `check_includes` (an explicit `include/`-adjacent vendored-header allowlist vs. a JSON pass-through ABI) — decision is not needed until an external extension consumes the evaluator.