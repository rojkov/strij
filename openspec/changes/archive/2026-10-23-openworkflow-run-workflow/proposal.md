# Proposal

## Why

The parser already models `run` tasks with their four process kinds and the `await` property, but the interpreter rejects every `run` body as unsupported. A workflow therefore cannot compose another workflow through the DSL; the only current way to run a subflow is the lower-level `call: strij` projection, which requires an external function reference and leaks the Strij task envelope into the document. `run.workflow` is the DSL-native composition primitive, and `await` is what makes a subflow either part of the parent's execution or a fire-and-forget branch.

## What Changes

- **`run.workflow` becomes executable.** `container`, `shell`, and `script` run processes remain recognized-but-unsupported (`UNIMPLEMENTED`); only the workflow process executes in this slice.
- **v1 uses an embedded-definition profile.** `namespace` must be the sentinel `embedded`. The run task's `input` is then a **definition-only string** holding the child workflow document, not subflow data. `name` and `version` are parser-required and inert (usable in diagnostics only). Any other `namespace` is recognized-but-unsupported, so the real resolver can arrive without changing the document shape.
- **Child input comes from the step pipeline.** Because `run.workflow.input` is consumed as the definition, the child's invocation input is the run step's transformed task input (after the step's own `input.from`). No separate data slot is added.
- **`await` controls the subflow lifecycle.** Absent means `true`: the parent suspends until the child reaches a terminal outcome and uses its final value as the step's raw output. `false`: the parent submits the child and continues immediately with the transformed input as its raw output; failures after a successful start are dropped.
- **Subflows are in-process nested interpreters, not child tasks.** A handler-owned runner starts a second interpreter for the embedded document. Awaited runs are linked to the parent and cancelled when the parent run is cancelled; detached runs are owned by the runner and survive the parent's completion or connection close. No child `task::Task` is submitted for the subflow itself.
- **Embedded definitions are validated before execution.** Because the definition is embedded, the pre-execution pass parses the child document, checks its expression mode, and recursively validates it (including any embedded children) before the parent's first step runs. A malformed child fails the parent with `MALFORMED_REQUEST`.
- **Nesting is bounded.** Recursive embedded `run.workflow` is guarded by an implementation-defined nesting limit so a self-referential document cannot exhaust the event-loop stack.
- **Out of scope:** external workflow resolution, `run.return`, `container`/`shell`/`script` processes, matching the embedded document's own `name`/`version`, subflow admission or placement, and streaming intermediate subflow results.

The embedded profile deliberately deviates from the DSL, which defines `run.workflow.input` as subflow data and identifies the subflow by namespace/name/version. v1 accepts that deviation to avoid introducing a workflow registry; the sentinel namespace keeps the extension explicit and replaceable.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `openworkflow-interpreter`: adds executable `run.workflow` tasks, the embedded-definition profile, `await` semantics, nested-runner lifecycle, and pre-execution validation of embedded child documents.

## Impact

- `src/nodeagent/extensions/task_handlers/openworkflow/interpreter.{hh,cc}` — validation of `run.workflow`, embedded-definition parsing, nesting guard, and the execution effects for awaited and detached subflows.
- `src/nodeagent/extensions/task_handlers/openworkflow/frame.{hh,cc}` — a `FrameContext` effect for running a subflow, plus the `RunBody` arm in the step pipeline; awaited completion reuses the existing suspension/reentrancy machinery.
- `src/nodeagent/extensions/task_handlers/openworkflow/` — a handler-owned nested-workflow runner that owns awaited and detached interpreters and exposes start/cancel.
- `src/nodeagent/extensions/task_handlers/openworkflow/openworkflow_task_handler.{hh,cc}` — construct and own the runner; keep detached runs alive past the parent connection.
- `test/nodeagent/extensions/task_handlers/openworkflow/` — interpreter, frame, handler, and e2e coverage for embedded execution, await on/off, cancellation, validation failures, recursion limits, and nested input/output shaping.
- `docs/testing-openworkflow.md` — the embedded profile and worked examples.
- No parser/SDK, config, proto, wire, `ChildTaskSubmitter`, `ResultReceiver`, or handler entrypoint changes.
