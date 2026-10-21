# Design

## Context

See proposal.md — Why. The interpreter now runs the per-step pipeline `raw input → if → input.from → body → raw output → output.as → data value`, with the interpreter supplying expression effects through `FrameContext` and `SequenceFrame` owning the cursor and completion paths. `$context` is still a constant: `ExpressionEngine` parses `{}` inside every `Run` and `EvaluateTypedSlot`, and `EvaluateCall` passes that empty object to `call` parameter projection.

Constraints shaping the approach:

- **Context is workflow-scope, not step state.** Every evaluation after the seed must see the latest context, including branch-free nested sequences and the document output.
- **Validation remains a separate document walk.** `export.as` syntax/wrapping and the fork-branch restriction must be rejected before the first submission.
- **Exactly-once completion.** A local child can complete inside `Submit`; export must run once on both the inline and resumed paths, after `output.as`.
- **`Jv` is move-only** with an explicit `Copy()` for shared handles; context must never be referenced after a move.
- **No new task kinds, schemas, secrets, or descriptor arguments.**

## Goals / Non-Goals

**Goals:**

- Introduce a single live context value owned by the interpreter, seeded once and replaced only by `export.as`.
- Place export as the last step-completion effect so it observes the shaped output and cannot change the data chain.
- Keep the expression API explicit about which values a slot may reference.
- Reject export inside fork branches before execution rather than define concurrency semantics prematurely.

**Non-Goals:**

- Branch-local contexts, context merging at a fork join, or ordering guarantees for concurrent writes.
- `set`, `export.schema`, `$task`/`$workflow`/`$runtime`/`$secrets`, or `loose` mode.
- Binding `$input` in `call`'s `with` expressions; this change only makes `$context` live there.
- Any change to `output.as` ordering, the data chain, or terminal-result delivery.

## Decisions

### D1 — The interpreter owns one `utils::Jv` context, seeded as `{}`

`WorkflowInterpreter` gains a `utils::Jv context_` member, initialized from `Jv::Parse("{}")` before `Start` evaluates document `input.from`. It is never null and is guaranteed to be a JSON object by D4. Expressions receive it by const reference; because `Jv` is move-only, any stored copy is made explicitly with `Copy()`.

`Document::input.from` runs before any task, so it observes the empty seed. Everything after it passes the same `context_`.

**Alternative rejected:** storing the context as a `StepDataValue` — it carries JSON-or-opaque, and would re-introduce the representation ambiguity this capability avoids.

### D2 — Export is a step-completion effect after `output.as`

`FrameContext` gains:

```cpp
virtual auto ExportTaskResult(const ::strij::openworkflow::NamedTask& step,
                              const StepDataValue& input,
                              const StepDataValue& transformed_output)
    -> absl::Status PURE;
```

`SequenceFrame` calls it after `TransformTaskOutput` succeeds and before the cursor advances, on both completion paths:

- `ApplyOutcome` (a `call` result, including the inline local-completion drain), and
- `advanceContainer` (a `do`/`fork` child returning `Completed`).

On error, the step fails through the existing `failStep` path. Export does not modify the data value, so the following step still receives the shaped output. Because the export call sits after `TransformTaskOutput`, it naturally sees the shaped output and cannot run on a skipped or failed step.

A successful export is committed before the cursor advances, so a later step observes it. If export fails, the step fails and no later step runs.

**Alternative rejected:** applying export inside `TransformTaskOutput` — it would conflate "produce the next data value" with "update workflow state", and make it easy to export twice on the inline path.

### D3 — Export sees the shaped output, the transformed input, and the current context

The export effect evaluates `export.as` with:

- subject = the transformed task output,
- `$output` = the same transformed output,
- `$input` = the step's transformed input,
- `$context` = the current context.

This follows the runtime-expression table: `export.as` runs against the transformed task output and may observe `$input` and `$output`. It is evaluated after `output.as`, so a task that declares both exports the shaped value, not the raw child or join value.

Validation applies the same rules as `output.as`: `export.as` is a typed slot, and both the transformed input and transformed output must be JSON. An opaque value fails the step with a subject error before the expression runs.

### D4 — The exported result must be a JSON object and replaces the context

`export.as` returns a `utils::Jv`; the interpreter checks `JvToValue(result).IsObject()`. A non-object result (array, scalar, null) is a run failure naming the step. On success, `context_ = std::move(result)`.

Replacing rather than merging keeps the operation explicit: authors merge with `$context + .` and replace with `.`, matching the reference examples. No implicit merge policy is invented.

### D5 — Expression evaluation takes an explicit argument bundle

Replace the per-call, implicit empty context with a small bundle passed to every evaluator entry point:

```cpp
struct ExpressionArguments {
  const utils::Jv* context{nullptr};
  const utils::Jv* input{nullptr};
  const utils::Jv* output{nullptr};
};
```

`TypedSlot` gains `kTaskExport`, and the slot-to-variable mapping becomes:

| Slot | Variables |
|---|---|
| `if` | `$context` |
| `input.from` | `$context` |
| task `output.as` | `$context`, `$input`, `$output` |
| task `export.as` | `$context`, `$input`, `$output` |
| document `output.as` | `$context` |

`Run`, `Evaluate`, `EvaluateTypedSlot`, and `ValidateTypedSlot` take the bundle (or the context pointer for literal-capable evaluation), and `ProjectCall` accepts the live context so `call`'s `with` values observe it. Compile-time variable names and run-time args stay aligned in the same order.

**Alternative rejected:** adding a `context` parameter in five separate places — an implicit argument-order contract already caused the empty-context duplication; the bundle names the values and keeps `input`/`output` optional.

### D6 — Validation rejects fork-branch export and checks `export.as`

`ValidateDocument` walks tasks with an `inside_fork_branch` flag:

- A task that declares `export` while the flag is true fails with `TASK_STATUS_UNIMPLEMENTED` before any submission.
- The flag is true for every task under `fork.branches`, including tasks in nested `do` bodies and nested forks; it is false for the fork task itself.
- `export.as` is validated as a typed slot through the same wrapped-only path as `if`/`input.from`/`output.as`.
- Unsupported task kinds remain rejected regardless of their other properties.

A malformed or bare `export.as` is `MALFORMED_REQUEST`; the fork-branch restriction is `UNIMPLEMENTED`. Both stop the run before the first child.

### D7 — Failure classification and terminal behavior are unchanged

- Bare/partial/invalid `export.as` → `MALFORMED_REQUEST` in the pre-execution pass.
- Export inside a fork branch → `UNIMPLEMENTED` in the pre-execution pass.
- Non-JSON transformed input/output, or a non-object export result → `INTERNAL` during execution, naming the step.
- A successful export does not alter the final result; document `output.as` still shapes the last data value and now reads the live context.

```
Start:
  context_ = {}
  data = document input.from ? eval(raw_input, {}) : raw_input

Step completion (call / do / fork):
  raw_output = child result / nested final value / fork join
  shaped     = TransformTaskOutput(step_input, raw_output)
  ExportTaskResult(step, step_input, shaped)   # context_ = object or step fails
  data = shaped; cursor++

Root Completed:
  result = document output.as ? eval(data, context_) : data
  succeed(result)
```

## Risks / Trade-offs

- **[Context becomes observable where it was previously always `{}`]** → This is the intended feature, but any document that read `$context` while ignoring `export` now sees exported values. Covered by scenarios and tests; no prior context-writing behavior existed.
- **[Fork-branch export is rejected rather than supported]** → Clear `UNIMPLEMENTED` at validation avoids nondeterministic last-writer-wins. Relaxing this later needs a merge/isolation design; it does not change the sequential contract.
- **[Export failure after a successful child]** → The child has already completed and is not cancelled; the step fails, consistent with `output.as` failures. Documented; no new cancellation path.
- **[Object-only context]** → Authors must export an object and merge explicitly. This is stricter than "any JSON", but matches the DSL's `map` type and keeps `$context.field` predictable.
- **[Expression argument plumbing touches call projection]** → `ProjectCall` and the literal-capable `Evaluate` gain a context parameter; update call-strij tests. No parameter-subject semantics change.
- **[Exactly-once export on inline completion]** → Export lives in `ApplyOutcome`, which is already the single completion sink for both inline and resumed results. Add a test where a local child completes inside `Submit` and the context is updated once.
- **[Context memory]** → `Jv::Copy()` per evaluation bumps the refcount; no deep copy. The context is replaced on export, so its lifetime is the run.

## Migration Plan

No wire, config, proto, or storage migration. Implement in dependency order while keeping the behavior suite green:

1. Expression engine: `ExpressionArguments`, live-context parameters, `TypedSlot::kTaskExport`, and slot variable mapping; update call-strij projection.
2. Validation: typed-slot check for `export.as`; `inside_fork_branch` tracking and the `UNIMPLEMENTED` rejection.
3. Interpreter state: seed `context_` and pass it to every evaluation.
4. Frame/context effect: `ExportTaskResult` on both completion paths.
5. Runtime export evaluation and object enforcement.
6. Tests and docs: interpreter, frame fake, expression, call-strij, e2e, and `docs/testing-openworkflow.md`.

Rollback is reverting the change: no external state or wire format is involved.

## Open Questions

None blocking. Branch-isolated or merged fork contexts, `set`, and the remaining runtime-expression arguments are later, independent changes.
