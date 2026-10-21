# Proposal

## Why

The parser models all seven common task properties, but the interpreter executes only a partial `input.from`: it shapes a `call` child body, is ignored by `do`/`fork`, and is not the subject of the task's own `with` expressions. `if`, task `output.as`, and document `output.as` are parsed and then silently ignored. That makes basic workflow authoring — conditional steps and shaped results — impossible, and the partial input pipeline would be inherited by every future task kind. Now that the frame layer is consolidated, the per-step data pipeline can be completed once, without new frames, timers, or task kinds.

## What Changes

- **Task `if` becomes executable.** It is evaluated against the raw task input before any other task expression, must yield a boolean, and on `false` performs a full short-circuit: no body, no `then`, no `output.as`, no `export`, and the data chain is unchanged. A non-boolean result or non-JSON subject is a run failure; a malformed or unwrapped expression is rejected during pre-execution validation.
- **Task `input.from` becomes the task's real input.** It is evaluated once against the raw input, its result is carried JSON-faithfully, and that transformed input becomes the subject of the task's `with` expressions, the seed of a nested `do`/`fork`, and the `call` child body. Previously `do`/`fork` ignored it and `call` used the raw data as the `with` subject.
- **Task `output.as` becomes executable.** It is evaluated against the raw output of a completed `call`, `do`, or `fork` step and its result becomes the data chain. `$input` names the transformed input and `$output` the raw output; both are required to be JSON, so a declared `output.as` over an opaque input or raw output fails that step.
- **Document `output.as` becomes executable.** It is evaluated against the last transformed output when the workflow completes successfully and its result becomes the final `TaskResult` body.
- **Typed-slot expressions are wrapped-only.** In `if`, `input.from`, task `output.as`, and document `output.as`, only a whole `${ ... }` string is an expression; bare text is a malformed-document error rather than a literal. This keeps one expression rule and makes the upstream examples explicit.
- **BREAKING:** A string-valued `input.from` result is now serialized as a JSON string in the child body (`"world"`) instead of raw bytes (`world`), so the data chain never turns a JSON string into opaque data. Documents that relied on the unquoted form must send the string as a bare JSON value or adjust the child.
- **Expression arguments.** `$input` is bound to the transformed task input and `$output` to the raw task output for `output.as`; `$context` remains present and empty. `$task`, `$workflow`, `$runtime`, and `$secrets` are out of scope.
- **Out of scope:** `input.schema`/`output.schema`/`export.schema`, `export` and a live `$context`, `then` and flow directives, `timeout`, the other nine task kinds, loose mode, and descriptor expression arguments.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `openworkflow-interpreter`: the data chain and task input pipeline gain `if`, `output.as`, and document `output.as`; `input.from` becomes the subject and seed for every task kind; the strict-expression rule is made explicit for typed slots; pre-execution validation covers the new expressions.
- `openworkflow-call-strij`: a `parameters` expression is evaluated against the task's transformed input (the data chain when no `input.from` is declared), and the child body carries the JSON-faithful transformed input.

## Impact

- `src/nodeagent/extensions/task_handlers/openworkflow/interpreter.{hh,cc}` — `if`/`output.as` execution, transformed-input pipeline, document output, expression-argument plumbing.
- `src/nodeagent/extensions/task_handlers/openworkflow/frame.{hh,cc}` — conditional/shape hooks around step advancement and container entry; nested sequences seeded with transformed input.
- `src/nodeagent/extensions/task_handlers/openworkflow/expression.{hh,cc}` — wrapped-only typed-slot program recognition, per-run `$input`/`$output` arguments, JSON-faithful results.
- `src/nodeagent/extensions/task_handlers/openworkflow/call_strij.{hh,cc}` — parameter subject and child-body representation.
- `test/nodeagent/extensions/task_handlers/openworkflow/` — interpreter, frame, expression, call-strij, and e2e coverage for conditionals, short-circuiting, output shaping, document output, and JSON-faithful transforms.
- No changes to configs, protos, wire messages, `ChildTaskSubmitter`, `ResultReceiver`, or the handler entrypoint.
