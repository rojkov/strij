# Proposal

## Why

The upcoming `open_workflow` task handler executes workflows defined with the Open Workflow DSL, whose default runtime expression language is jq (`document.evaluate.language`, strict/`${ }` and loose modes, `$workflow`/`$task` variables). To run those expressions with reference semantics — and so operator-declared alternative evaluator implementations can be swapped in per language — the codebase needs an embeddable, libjq-backed expression evaluation extension category. Today there is no expression engine in the tree: `include/` headers may only reference its own tree, absl, protobuf, and std, and we deliberately keep the evaluator out of the public surface until an external extension consumes it.

## What Changes

- **Add `libjq` as a build dependency**: `jq` 1.8.2 via `http_archive` + `configure_make` (the `liburing` pattern), built with `--with-oniguruma=builtin` and `--disable-maintainer-mode`; exposed as a `cc_library` with `jq.h`/`jv.h`.
- **Add an RAII `Jv` wrapper** (`strij::utils::Jv`): move-only, refcount-correct lifetime for the libjq value type, JSON parse/serialize helpers as thin wrappers over `jv_parse`/`jv_dump`.
- **Add an `Evaluator` abstract interface + `EvaluatorFactory` extension category** (internal to `src/` for now): `Evaluator` is a compiled-expression handle (`Compile(source, argnames)` → runnable, `Run(input, args)` → all outputs as `std::vector<Jv>`); `EvaluatorFactory` mirrors the `TaskHandlerFactory`/scheduler-factory shape (`Name`, `CreateEmptyConfigProto`, `Create`, `ParseConfig`) with a minimal `EvaluatorDeps` bundle.
- **Add the bundled `JqEvaluator`** (`strij::extensions::evaluators::JqEvaluator` + factory under `src/common/extensions/evaluators/jq/`, config proto under `api/common/extensions/evaluators/jq/`), registered via the existing `REGISTER_FACTORY`/`Registry` machinery producing faithful jq semantics (compile once, run many, error messages via `jq_get_error_message`).
- **Introduce `src/common/loaders/` as the sanctioned home of config→instance bridge glue**, with `strij::loaders::CreateEvaluator(...)` — the first loader; `scheduler_loader` is explicitly **out of scope** (a follow-up change reworks it into this layout).
- **Update the repo doctrine docs** (`AGENTS.md`, `docs/extension-author-guide.md`) and the `extension-api-surface` spec for the new `loaders/` layer.
- **BREAKING**: none to existing behavior — new targets and directories only; `test/consumer` is untouched.

## Capabilities

### New Capabilities

- `expression-evaluation`: The shared extension category for runtime expression evaluation — the `Evaluator` compiled-program contract, the `EvaluatorFactory` extension shape and `EvaluatorDeps` bundle, Registry-based registration, the libjq-backed `JqEvaluator`, and the config→instance loader. Deliberately scoped to the engine itself, not to any consuming handler.

### Modified Capabilities

- `extension-api-surface`: The repository layout requirement gains a third-level `loaders/` directory under `src/common/` (shared config→instance bridge glue), and the namespace doctrine gains the bare `strij::loaders` concern namespace alongside `strij::extensions`/side namespaces.

## Impact

- **Build**: `MODULE.bazel` + root `BUILD.bazel` gain `jq` 1.8.2 (`http_archive` + `configure_make`, sha256 `71b8d6e8f5fe81f6c6d0d110e3892251f6ce76ed095abd315e26e6e1193af3af`); `rules_foreign_cc` already present. `//:libjq` static lib + headers.
- **New code**: `src/common/core/utils/jv.hh` (`strij::utils::Jv`), `src/common/extensions/evaluators/` (interface + deps), `src/common/extensions/evaluators/jq/` (impl + factory), `api/common/extensions/evaluators/jq/jq.proto`, `src/common/loaders/evaluator_loader.hh` (`strij::loaders`). Public `include/` surface is **unchanged**.
- **Tooling**: `make check_namespaces` and `make check_includes` must still pass — the new files open no side namespaces and live under `src/`, so neither checker needs modifying for this change.
- **Known limitation**: libjq has no instruction-budget API — an adversarial-but-valid jq expression can spin the event loop. Documented as a v1 limitation; no timeout/threading in scope.
- **Deferred**: open_workflow handler, per-document language selection, `${ }` scanning, strict/loose semantics, scheduler_loader rework, public `include/` promotion of `Evaluator` (plus libjq include-purity exemption).