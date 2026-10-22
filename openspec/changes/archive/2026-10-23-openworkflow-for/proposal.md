# Proposal

## Why

The Open Workflow interpreter executes `call`, `do`, `fork`, `set`, and `switch`; `for` is parsed into `ForBody`/`ForSpec` and then rejected as `UNIMPLEMENTED`. `for` is the reference's iteration primitive: without it a document cannot apply a body to every element of a collection, and the reference's own `for` conformance scenario cannot run.

## What Changes

- **`for` becomes executable.** A `for` task evaluates its `for.in` collection against the transformed task input, then runs its `do` body once per item. The body's data chain carries across iterations: the first iteration is seeded with the transformed input, each iteration's result seeds the next, and the loop's raw output is the final accumulator.
- **Loop variables become lexical expression bindings.** `for.each` (default `item`) and `for.at` (default `index`) bind the current item and zero-based index as jq variables (`$item`, `$index`) for every expression in the body, including nested `do`, `fork`, and `for` bodies. Bindings are scoped to the body and survive a suspension, so a child result can be shaped with expressions that reference the loop variables.
- **`for.in` is a collection slot.** A string form is a whole-value `${ }` expression evaluated against the transformed input; a non-string form is a literal-capable value tree whose inline array elements may contain `${ }` expressions and whose bare strings stay literal. An array iterates, `null` iterates zero times, and any other runtime result fails the step.
- **`for.while` is an expression-typed slot.** It is evaluated after each completed iteration against the accumulator, with the iteration's loop variables bound, and must yield a boolean; a false result stops before the next item. A bare or malformed `while` is a malformed document.
- **`for` joins the ordinary step pipeline.** `if` can skip the whole loop; `input.from` seeds it; `output.as`, `export.as`, and `then` apply after the loop completes, with `$input`/`$output` bound as for every composite task. `end` inside the body concludes the workflow and skips the `for` task's own shaping.
- **Validation learns loop scopes.** The pre-execution pass validates `for.in`, `while`, the loop variable names, and every task in the body with those names visible; it rejects invalid names, collisions, non-array inline collections, and malformed expressions before any child is submitted.
- **`for` is allowed inside a fork branch** and inherits the existing `export`-inside-a-branch rejection for its body.
- **Synchronous iteration is bounded.** A `for` over a large collection that never suspends is bounded per synchronous stretch and fails with `INTERNAL` rather than starving the event loop.
- **Out of scope:** `emit`, `listen`, `raise`, `run`, `try`, `wait`, `timeout`, loose expression mode, and any new runtime-expression arguments beyond the loop variables.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `openworkflow-interpreter`: adds the executable `for` task (accumulator semantics, lexical loop variables, `for.in`/`for.while`, `for` in the composite-task and branch-task lists, loop-aware validation, and a synchronous iteration bound).

## Impact

- `src/nodeagent/extensions/task_handlers/openworkflow/expression.{hh,cc}` — lexical local-variable binding for compile and evaluation, plus `for.in`/`for.while` typed slots.
- `src/nodeagent/extensions/task_handlers/openworkflow/frame.{hh,cc}` — the lexical scope chain on frames, a sequential `ForFrame`, and `FrameContext` effects for collection and `while` evaluation.
- `src/nodeagent/extensions/task_handlers/openworkflow/interpreter.{hh,cc}` — `for` execution effects, scope-aware validation, and loop variable name checks.
- `test/nodeagent/extensions/task_handlers/openworkflow/` — frame, interpreter, expression, and e2e coverage for iteration, accumulator chaining, loop variables, nesting, `while`, empty/null/non-array collections, validation, suspension, and the iteration bound.
- `docs/testing-openworkflow.md` — operator-facing examples for `for`, including a nested loop.
- No changes to the parser/SDK (`ForBody`, `ForSpec`, and `Task::body_` already exist), configs, protos, wire messages, `ChildTaskSubmitter`, `ResultReceiver`, or the handler entrypoint.
