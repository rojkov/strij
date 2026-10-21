# Proposal

## Why

The parser and interpreter model `export`, but the property is never executed and `$context` is hardcoded to `{}` on every expression evaluation. Authors therefore cannot carry a workflow-scope value — an auth token, a resolved ID, a shared setting — from one step to another without threading it through the data chain and reshaping every intermediate output. The per-step data pipeline is now settled, so the context half can be added without introducing new task kinds or frame shapes.

## What Changes

- **`$context` becomes a live workflow-scope value.** It is seeded as an empty JSON object at start, persists across steps, and is passed to every runtime expression. It remains distinct from the data chain.
- **Task `export.as` becomes executable.** After a step's `output.as` transformation, the interpreter evaluates `export.as` against the transformed task output, with `$input` (the transformed task input), `$output` (the transformed task output), and the current `$context` available. The JSON object result replaces the context. A task with no `export`, or an `export` with no `as`, leaves the context unchanged.
- **Export applies to completed `call`, `do`, and `fork` steps.** It runs only for a step that completed successfully and was not skipped by `if`; a failed or skipped step does not export.
- **Export inside a fork branch is excluded from this slice.** Fork branches run concurrently and the DSL does not define how simultaneous context writes combine, so a task declaring `export` inside a fork branch is rejected during validation as unimplemented. A `fork` task's own `export` (outside its branches) is supported. **BREAKING** relative to today's silent no-op: such documents now fail validation instead of ignoring the property.
- **`export.as` is a wrapped-only expression slot.** It joins `if`, `input.from`, `output.as`, document `input.from`, and document `output.as`: only a whole `${ }` string is an expression, and bare text is a malformed document. The exported result must be a JSON object; any other result is a run failure.
- **The live context reaches existing slots.** `if`, `input.from`, task `output.as`, document `output.as`, and a `call`'s `with` expressions observe the current context instead of an empty object. Document `input.from` runs before any export and therefore still observes the empty seed.
- **Out of scope:** `set` (which writes the data chain, not the context), `export.schema`, branch-isolated or merged fork contexts, `$task`/`$workflow`/`$runtime`/`$secrets`, and `loose` mode.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `openworkflow-interpreter`: `$context` becomes a live, export-writable workflow value; adds the export requirement, extends the typed-slot and validation requirements to `export.as`, and updates document `output.as` to observe the current context.

## Impact

- `src/nodeagent/extensions/task_handlers/openworkflow/interpreter.{hh,cc}` — a seeded context member, the export effect after output shaping, context threading into every expression call, and validation of `export.as`.
- `src/nodeagent/extensions/task_handlers/openworkflow/frame.{hh,cc}` — a new `FrameContext` export effect invoked on both the resumed-call and container completion paths; the validation walk marks fork-branch tasks so export there can be rejected.
- `src/nodeagent/extensions/task_handlers/openworkflow/expression.{hh,cc}` — a `TypedSlot::kTaskExport` slot, and replacing the hardcoded empty context with a caller-supplied context in `Run`, `Evaluate`, and `EvaluateTypedSlot`.
- `src/nodeagent/extensions/task_handlers/openworkflow/call_strij.{hh,cc}` — thread the live context into `with` parameter projection.
- `test/nodeagent/extensions/task_handlers/openworkflow/` and `docs/testing-openworkflow.md` — interpreter, frame, expression, call-strij, e2e coverage, and documentation for export and context.
- No changes to configs, protos, wire messages, `ChildTaskSubmitter`, `ResultReceiver`, or the handler entrypoint.
