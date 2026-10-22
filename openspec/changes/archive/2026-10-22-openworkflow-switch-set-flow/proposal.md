# Proposal

## Why

The Open Workflow interpreter executes only `call`, `do`, and `fork`; `switch`, `set`, and the common `then` flow directive are parsed and then rejected or silently ignored. Without them a workflow cannot branch on data, replace the data chain without a child task, or direct control flow — the three primitives the reference's own `set`, `flow`, and `switch` conformance scenarios are built from.

## What Changes

- **`set` becomes executable.** A `set` task evaluates its data against the transformed task input and replaces the data chain. The map form is literal-capable (a string is an expression only when wholly `${ }`); the scalar form is the direct expression form and follows the same strict rule. `set` always produces JSON and never writes `$context` (export remains the only context writer).
- **`switch` becomes executable.** A `switch` task evaluates its cases in declaration order against the transformed task input, selects the first matching case, and applies that case's `then` as the step's flow directive. A case without `when` is a deferred fallback; the switch task's own `then` is the fallback when no case matches. The switch's raw output is its transformed input.
- **`switch.when` is an expression-typed slot.** Like `if`, it accepts only a whole `${ }` value, must yield a boolean, and binds `$context`. Bare `when` values are a malformed document. Loose mode remains out of scope.
- **`Task.then` becomes executable.** `continue` (or absent), `exit`, `end`, and a task name in the same scope are supported; jumps may move forward or backward. The directive runs after `output.as` and `export.as`, and never on a step skipped by `if` or a failed step. `exit` completes the current scope; `end` concludes the workflow from any depth, skipping enclosing transforms.
- **Validation resolves flow directives.** A missing switch-case `then`, more than one default case, an unknown or cross-scope `then` target, a duplicate task name in a scope, and malformed `when`/`set` expressions are reported before the first child is submitted.
- **Synchronous execution is bounded.** Because backward jumps are legal, a document can loop without ever yielding. Each sequence bounds the number of steps it advances in one synchronous stretch and fails the workflow with `INTERNAL` when the bound is exceeded, protecting the event loop. Loops that suspend on a child are unaffected.
- **Fork branch task kinds widen.** Branch tasks may now be `set` or `switch` in addition to `call`, `do`, and `fork`; the existing `export`-inside-a-branch rejection is unchanged.
- **Out of scope:** `emit`, `for`, `listen`, `raise`, `run`, `try`, `wait`, `timeout`, catch flow directives, loose expression mode, and new runtime-expression arguments.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `openworkflow-interpreter`: adds flow directives (`then`), the `switch` and `set` task kinds, the synchronous-transition guard, `switch.when` as a typed slot, and the expanded validation and branch-task-kind rules.

## Impact

- `src/nodeagent/extensions/task_handlers/openworkflow/frame.{hh,cc}` — inline `set`/`switch` step handling, flow-directive application on both the resumed-call and container completion paths, `Ended` propagation through sequences and forks, per-sequence transition accounting, and execution-time scope resolution of `then` targets.
- `src/nodeagent/extensions/task_handlers/openworkflow/interpreter.{hh,cc}` — `set`/`switch` execution effects, `switch` case evaluation, `then` validation, scope-aware validation (including one-task fork-branch scopes), and `switch.when` compilation.
- `src/nodeagent/extensions/task_handlers/openworkflow/expression.{hh,cc}` — `TypedSlot::kSwitchWhen` and its variable mapping.
- `test/nodeagent/extensions/task_handlers/openworkflow/` — interpreter, frame, expression, and e2e coverage for set/switch/flow directives, `end` propagation, scope validation, backward jumps, and the transition guard.
- `docs/testing-openworkflow.md` — operator-facing examples for `set`, `switch`, and `then`.
- No changes to the SDK/parser (`SwitchBody`, `NamedCase`, `SetBody`, and `Task::then_` already exist), configs, protos, wire messages, `ChildTaskSubmitter`, `ResultReceiver`, or the handler entrypoint.
