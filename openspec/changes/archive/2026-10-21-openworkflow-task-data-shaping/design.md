# Design

## Context

See proposal.md — Why. The current execution path is `SequenceFrame::Advance` → `FrameContext::ExecuteCall` for `call` steps and `materializeChild`/`child_->Advance` for `do`/`fork`. The frame owns the cursor and data value; the interpreter owns parsing, validation, expressions, submission, outcome routing, and terminal result delivery. `ApplyOutcome` currently writes the child body straight into the data chain, and container entry passes `data_` straight into the nested frame. Expression evaluation binds only `$context`, always to `{}`.

Constraints shaping the approach:

- **Validation stays a separate document walk.** Every data-independent failure must occur before the first submission. `if`/`input.from`/`output.as` syntax and wrapper checks belong there.
- **`Submit` is not a barrier.** A local child can complete inside `Submit`; the `kAdvancing`/`pending_` reentrancy gate and the per-child inline drain must keep working, now applying `output.as` exactly once.
- **The document outlives the run.** Frames already store spans/views into it; storing the current step's transformed input is the only new per-step state.
- **No schema, timer, or context capability is being added.** `$context` remains empty, and `input.schema`/`output.schema`/`export.schema` remain parsed and ignored.
- **The parser already models everything.** No parser or model change is required; only the interpretation of typed slots and execution change.

## Goals / Non-Goals

**Goals:**

- Make the per-task pipeline explicit and uniform for `call`, `do`, and `fork`:
  `raw input → if → input.from → body → raw output → output.as → data value`.
- Keep frame control flow and interpreter effects separated through a narrow widened `FrameContext`.
- Preserve exactly-once child-result handling, including inline local completion, fork concurrency, and failure composition.
- Make JSON-vs-opaque boundaries explicit, with JSON-faithful transforms and no invented representations.

**Non-Goals:**

- `then`/flow directives, `switch`, or any change to cursor semantics beyond skip and advance.
- `export`, a live `$context`, or `set`.
- Schemas, `timeout`, `wait`, or other task kinds.
- `$task`, `$workflow`, `$runtime`, `$secrets`, or `loose` mode.
- Cancelling a child when a later output transform fails; the child has already completed.

## Decisions

### D1 — SequenceFrame drives the step pipeline; FrameContext owns the expression effects

`SequenceFrame` keeps control flow (cursor, skip, suspend, advance). The interpreter keeps all expression work behind three new `FrameContext` methods, mirroring the existing `ExecuteCall`/`ForgetChild` seam:

```cpp
class FrameContext {
public:
  virtual auto EvaluateCondition(const NamedTask&, const StepDataValue& raw)
      -> absl::StatusOr<bool> PURE;
  virtual auto TransformTaskInput(const NamedTask&, const StepDataValue& raw)
      -> absl::StatusOr<StepDataValue> PURE;
  virtual auto TransformTaskOutput(const NamedTask&, const StepDataValue& input,
                                   const StepDataValue& raw_output)
      -> absl::StatusOr<StepDataValue> PURE;
  // existing ExecuteCall / ForgetChild
};
```

`ExecuteCall` takes the transformed input (`const StepDataValue&`) instead of reading `owner.Data()`: it is the subject for `with` and the child body source. `materializeChild`/`child_->Enter` take the transformed input. `ApplyOutcome` gains a `FrameContext&` so the asynchronous completion path can apply `output.as`; the interpreter already has itself available at every call site.

`SequenceFrame` stores `std::optional<StepDataValue> step_input_` for the current step, set before dispatch and cleared after the step completes or advances. That is what lets a resumed child apply `output.as` with the right `$input` without re-evaluating `input.from`.

**Alternatives rejected:** putting `if`/`input.from`/`output.as` inside `ExecuteCall` — containers have no call, and the async path would need a second copy; applying `output.as` in `OnChildOutcome` — the interpreter would need to reach back into the frame's current step and input.

### D2 — Typed slots are evaluated JSON-faithfully, and `input.from` runs once

A new helper evaluates a typed slot (`if`, `input.from`, `output.as`, document `output.as`) and returns a JSON value; serialization always goes through `ValueToJv(...).Dump()`. This replaces the current `renderFrom` string-special case, which returned a string result unquoted and therefore made it opaque to `StepDataValue::FromBytes`.

- `input.from` is evaluated once per step entry against the raw data value; the result is stored in `step_input_`.
- With no `input.from`, `step_input_` is the raw data value unchanged.
- A `call: strij` child body is `step_input_.Bytes()`; a JSON string is therefore quoted.
- A nested `do`/`fork` is entered with `step_input_`.
- Document `input.from` uses the same helper, so its string results are JSON strings too.

**Alternatives rejected:** keeping the unquoted string form for child bodies — the data chain would still turn a JSON string into opaque bytes, and `$input` would be ambiguous; storing both a JSON value and body bytes — two representations that can drift.

### D3 — Expression-typed slots are validated as wrapped-only expression trees

`ExpressionEngine` gains a typed-slot validation path separate from the existing literal-capable `Validate`:

- For a top-level string, require `IsExpression`; a bare string or a partial wrapper is an `InvalidArgumentError` naming the slot.
- For the object form, recurse: every nested string must be wrapped; non-string scalars remain literal values.
- Compile every discovered program before execution, using the slot's expression arguments (D4).

At run time the existing `Evaluate` is used; because validation guarantees every string in a typed slot is wrapped, it evaluates exactly the intended programs. `Validate` remains unchanged for literal-capable value slots such as `call.with` and `metadata`.

**Alternatives rejected:** treating bare typed-slot text as literal (current behavior) — a bare `if` would silently mean "always true"; consuming upstream bare examples — rejected in exploration in favor of one uniform rule.

### D4 — Expression arguments are selected per slot

Extend the expression API from "always `$context`" to a small slot-scoped variable set aligned with the evaluator's existing `variable_names`/`args` contract:

| Slot | Variables |
|---|---|
| `if` | `$context` |
| `input.from` | `$context` |
| `output.as` | `$context`, `$input`, `$output` |
| document `output.as` | `$context` |

`$context` remains an empty JSON object. `$input` is the JSON value of `step_input_`; `$output` is the JSON value of the raw output, which is also the expression subject. Because `output.as` declares both variables, the interpreter requires both values to be JSON before evaluating (D6). `$task`, `$workflow`, `$runtime`, and `$secrets` are deliberately not declared.

**Alternatives rejected:** always declaring every argument and binding null when unavailable — silently invents a value and hides author mistakes; exposing opaque bytes as a JSON string — also invents a representation.

### D5 — `output.as` is applied on every completion path, exactly once

`output.as` is applied in `SequenceFrame::ApplyOutcome` (call results, including inline local completion) and in `advanceContainer` after a nested `do`/`fork` returns `Completed`. Both consume the raw output and set the data value to the transformed result before advancing the cursor. A failure in the transform is a step failure (`failStep`, origin = this sequence), so fork failure composition and root failure reporting work unchanged.

A child that fails never reaches `output.as`; the existing failure path is untouched.

```
Advance:
  if condition false: data unchanged, cursor++, continue
  step_input_ = TransformTaskInput(raw data)
  call: ExecuteCall(step, step_input_)
          -> Suspend; later ApplyOutcome(outcome, ctx)
             raw = outcome.body
             data_ = TransformTaskOutput(step, step_input_, raw)
             cursor++, clear step_input_
  do/fork: child_->Enter(step_input_); child_->Advance
             Completed(raw) -> data_ = TransformTaskOutput(step, step_input_, raw)
             cursor++, clear step_input_
```

### D6 — Failure classification follows the existing malformed-vs-run split

- Unwrapped, partial, or syntactically invalid typed-slot expression → `MALFORMED_REQUEST` in the pre-execution pass, before any submission.
- Non-boolean `if` result, non-JSON subject/input/output, or any other data-dependent transform failure → `INTERNAL` during execution, naming the step.
- Document `output.as` failure → `INTERNAL` with a workflow-level message (no step).
- Child failure status propagation and fork branch naming are unchanged.

This matches the existing taxonomy in `task-result-status`: malformed document is the caller's fault and detected before children; wrong data is a run failure.

### D7 — Document `output.as` runs once at root completion

`drive` handles the root `Completed(value)` by applying document `output.as` (subject = final data value, `$context` empty) and then calling `succeed` with the transformed value; a transform failure calls `fail(INTERNAL, "", message)`. No `output.as` preserves today's behavior. The transformation runs exactly once because `drive` only terminalizes a root `Completed` once.

### D8 — Skipped steps do not touch the pipeline

Condition `false` advances the cursor before `TransformTaskInput`, so no `input.from`, body, `output.as`, `then`, or `export` runs and `data_` is unchanged. The step is never represented as a synthetic output; the next step simply receives the same raw input. This is the full short-circuit recorded in exploration and keeps the reference guard-task shape (`raise` with `then: end`) from ending the workflow when the guard is false.

### D9 — Validation order and state remain a separate walk

`ValidateDocument` continues to walk every top-level, nested-`do`, and fork-branch task. For each task it now also validates the typed slots. Unsupported task kinds remain rejected regardless of an `if` that would skip them at run time: the document is still not executable in this slice, and validation reports that before any child is submitted.

## Risks / Trade-offs

- **[Behavior change: quoted string bodies]** → A string `input.from` result now reaches the child as `"world"` instead of `world`. Documented in the proposal; update `docs/testing-openworkflow.md` and e2e expectations; a script that wants the raw text can read the JSON string or the workflow can call a shaping child. This is the only intentional wire-visible change for `call: strij`.
- **[Wrapped-only rejects upstream bare examples]** → Bare `if`/`from`/`as` now fail validation with a clear message naming the slot. No working behavior is lost because those strings were previously literal anyway; provide the `${ }` spelling in tests/docs.
- **[Opaque input with `output.as`]** → Because `$input` is declared, both transformed input and raw output must be JSON; an opaque chain cannot be shaped. Over-strict for expressions that never read `$input`, but avoids inventing a representation; can be relaxed later with reference analysis.
- **[Async correctness]** → `step_input_` must be set before submission and cleared exactly once; inline local completion must not apply `output.as` twice. Cover with an inline-completion test and a resumed-result test.
- **[FrameContext growth]** → Frames gain three effects. Keep them expression-only, keep `frame.hh` independent of the interpreter, and extend the fake context in `frame_test.cc`.
- **[Failure-message drift]** → New errors name the step and slot consistently. Add focused tests for bare slots, non-boolean conditions, opaque subjects, and document-output failures.

## Migration Plan

No data migration and no config/proto changes. Implement in dependency order while keeping the behavior suite green:

1. Expression engine: typed-slot validation, JSON-faithful evaluation helper, per-slot variable sets.
2. FrameContext: add the three effects and the transformed-input parameter to `ExecuteCall`/`materializeChild`; thread `FrameContext&` into `ApplyOutcome`.
3. `input.from` pipeline for `call`, `do`, and `fork`; JSON-faithful child body.
4. `if` condition and full short-circuit.
5. Task `output.as` on call and container completion paths.
6. Document `output.as` at root completion.
7. Tests and docs (interpreter, frame fake, expression, call-strij, e2e, testing guide).

Rollback is reverting the change: no wire, config, proto, or storage state is involved. Existing documents that do not use the new properties keep their behavior except for the intentional JSON-faithful string body change.

## Open Questions

None blocking. Relaxing the opaque-input restriction for `output.as` and widening expression arguments to `$task`/`$workflow` are later, independent changes.
