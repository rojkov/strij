# Design

## Context

See `proposal.md` — Why. The constraints that shape the approach:

- The parser already produces `RunBody { process, optional await, optional return }`
  with `WorkflowProcess { namespace, name, version, optional input }`
  (`src/openworkflow/task.hh`). The interpreter rejects every non-`call`/`do`/
  `fork`/`for`/`set`/`switch` body as unsupported (`interpreter.cc:385`,
  `frame.cc:447`).
- The frame tree is the execution model. `SequenceFrame` owns a cursor and a
  data value; an async step calls a `FrameContext` effect, registers a synthetic
  child id through `owner_by_child_`, suspends, and resumes through
  `OnChildOutcome`/`ApplyOutcome`. The `pending_` map handles outcomes that
  arrive while the interpreter is inside `Submit` (`interpreter.hh`).
- `ChildResultReceiver` holds a `weak_ptr` back to the interpreter, so a late
  child outcome is dropped rather than touching freed state.
- `ChildTaskSubmitter` is the path for independently scheduled child tasks. A
  local child is normally run on the same node by the default scheduler, and may
  be forwarded when local admission refuses.
- The handler owns top-level runs in `runs_`, registers a close callback, and
  reaps finished runs. `FunctionResolver` and `LoadWorkflowArtifact` are
  handler-level services.
- The parser's opaque `Value::Object` is a `std::map`, so reconstructing an
  embedded document from a `Value` would lose object key order.

## Goals / Non-Goals

**Goals:**

- Execute `run.workflow` through the existing frame pipeline with one new
  `FrameContext` effect and a `RunBody` arm.
- Preserve the subflow's own document semantics: its own `input.from`,
  `$context` seed, output shaping, and child tasks.
- Give `await` two distinct, observable lifecycles without duplicating the
  suspension machinery.
- Validate embedded child documents before the parent executes any step.
- Keep the v1 profile replaceable by a real workflow resolver later.

**Non-Goals:**

- External workflow resolution, artifact loading, or function-plane integration.
- `run.container`, `run.shell`, `run.script`, and `run.return`.
- A subflow admitted or placed as an independent task.
- Streaming a subflow's intermediate results.
- Inheriting the parent's `$context`.
- Matching the run task's inert `name`/`version` to the embedded document.
- Persistence, resumption across process restart, or cancellation of a nested
  run's own outstanding child tasks.

## Decisions

### D1 — `run.workflow` is an in-process nested interpreter, not a child task

The run step starts a second `WorkflowInterpreter` owned by the handler. The
parent sees it as an async child: the completion is delivered as a
`ChildOutcome` through the existing `owner_by_child_`/`pending_` route.

```
Interpreter          SequenceFrame          Runner              Nested Interpreter
     |                     |                   |                        |
     |-- Advance --------->|                   |                        |
     |                     |-- ExecuteRun ---->|                        |
     |                     |                   |-- StartEmbedded ------>|
     |                     |<-- Suspend(id) ---|                        |
     |                     |                   |                        |-- steps/children
     |                     |                   |<-- completion ---------|
     |<-------------------- OnChildOutcome(id) |                        |
     |-- ApplyOutcome ---->|                   |                        |
     |                     |-- output.as/export.as/then              |
```

Why not a child task (`type: openworkflow` through `ChildTaskSubmitter`)?

- The node running the parent already has the `openworkflow` handler, so the
  local scheduler would normally run the child on the same node anyway. The
  "distributed" benefit only appears when local admission refuses and forwarding
  succeeds.
- An independently scheduled child consumes another `openworkflow` concurrency
  slot. With a small configured capacity, a parent could not invoke a subflow.
- The reference treats a subflow as part of the parent execution context. An
  in-process runner can cancel an awaited nested run directly; a child task
  cannot be cancelled without new protocol.
- The embedded definition needs no resolver and no artifact load. Going through
  the scheduler would add admission, a receiver, a task id, and forwarding for
  no lookup benefit.

Alternatives rejected: flattening the embedded tasks into the parent frame
(loses the child document's own input/output/context boundaries); running the
handler's `HandleTask` directly with a fake sender (creates a handler/interpreter
cycle and duplicates result classification).

The detached (`await: false`) case is the one that may later become a child
task, because detached work is by definition an independent execution. That
revisit is explicitly out of scope.

### D2 — v1 embedded profile: sentinel namespace, definition string, inert identity

For `namespace: embedded`:

- `run.workflow.input` is the child document text, not subflow data.
- The child's invocation input is the run step's transformed task input, so the
  step's own `input.from` is the only data-shaping mechanism.
- `name` and `version` are inert. They remain parser-required, and `name` may be
  used to label diagnostics.
- Any other `namespace` is `UNIMPLEMENTED`.
- A non-string or missing `input` is `MALFORMED_REQUEST`.
- A declared `return` is `UNIMPLEMENTED`.

Using a string rather than a `Value` tree is deliberate: it preserves object key
order (the parent parser's `Value::Object` is a `std::map`), preserves expression
text verbatim, and needs no `Value` -> JSON round-trip. It is also a natural
container for a whole document.

Alternative rejected: `input` as a map with `input[name]` holding the child
definition. It overloads a data slot with a registry, invites collisions, and
reconstructing the child from a `Value` loses ordered map-form task bodies.

### D3 — Embedded definitions are parsed and validated in the pre-execution pass

`ValidateDocument` gains a `RunBody` arm that, for an embedded workflow:

1. requires `namespace == "embedded"` and a string `input`;
2. parses the string with the existing SDK parser;
3. checks the child's `evaluate.mode`;
4. recursively validates the child document and all of its embedded children;
5. carries the parent step name and embedded `name` into the failure message.

```
ValidateDocument(parent, engine, depth)
  for each task:
    if run.workflow.embedded:
      parse input string -> child Document
      validate child evaluate.mode
      ValidateDocument(child, engine, depth + 1)
```

A failure is reported before the parent's first step and before any child
submission. It applies even when `if` would skip the step or `await` is false,
matching the existing rule that every declared task is validated.

A depth guard (implementation-defined, generous) turns recursive embedding into
an `UNIMPLEMENTED` pre-execution failure rather than a stack overflow.

Alternative rejected: validating the embedded child only when the run step
starts. The definition is already in memory; deferring would let a malformed
document start the parent and submit unrelated work first.

### D4 — Frame integration reuses the call suspension pattern

`FrameContext` gains one effect, mirroring `ExecuteCall`:

```text
ExecuteRun(owner, step, transformed_input, scope) -> absl::Status
```

`SequenceFrame::Advance` adds a `RunBody` arm:

```text
await true/default:
  ExecuteRun suspends the owner under a synthetic child id and starts the nested run
  -> Suspended, or an inline outcome through pending_/ApplyOutcome
  -> child final body becomes the step's raw output
  -> ordinary output.as / export.as / then pipeline

await false:
  ExecuteRun starts the nested run but does not suspend
  -> step completes immediately with a copy of the transformed input
  -> ordinary output.as / export.as / then pipeline
  -> the nested run's later outcome has no owner and is dropped
```

A failure to start the nested run fails the step as `INTERNAL`; this can only be
an internal failure because syntax, validation, and mode are already checked
pre-execution.

The synchronous transition budget still applies: a non-awaited step consumes one
transition, and a loop of non-awaited subflows cannot wedge the event loop.

### D5 — The handler owns a nested-workflow runner

A handler-owned runner is the only component that owns nested interpreters:

```text
OpenWorkflowTaskHandler
  +-- runner
  |     +-- awaited runs: tracked by synthetic id, cancellable
  |     +-- detached runs: owned until completion
  |     +-- depth guard
  +-- runs_ (top-level invocations)
        +-- WorkflowInterpreter (parent)
              +-- synthetic child id -> nested run
```

Runner operations:

```text
StartEmbedded(definition_text, invocation_input, on_done, depth) -> StatusOr<Handle>
Cancel(handle)
```

- `StartEmbedded` parses the document again, constructs a
  `WorkflowInterpreter`, installs it in the runner, and starts it.
- The nested interpreter receives the same `ChildTaskSubmitter` and the same
  evaluator configuration as its parent, so its own `call`/`run` steps work.
- Awaited runs are tracked by the parent interpreter (its `owner_by_child_`
  entries). `WorkflowInterpreter::Cancel` asks the runner to cancel those
  synthetic ids. Detached runs are not tracked by the parent, so they survive.
- A detached run's completion is a no-op that lets the runner erase it.
- A nested interpreter receives a `weak_ptr` back to its parent for completion,
  exactly like `ChildResultReceiver`, so a late outcome cannot touch freed
  parent state.
- The runner itself must be reachable from nested interpreters (they can spawn
  their own subflows) without a cycle. The interpreter holds a non-owning
  reference; the handler owns the runner and all interpreters, with destruction
  order ensuring the runner outlives interpreter teardown. Awaited cancellation
  is explicit; no interpreter destructor calls the runner.

```
Awaited:                        Detached:

parent --child id--> runner     parent --start--> runner
runner --> nested (live)                        runner --> nested (live)
parent Cancel -> runner.Cancel(id)              parent completes/ends/cancels
runner cancels nested                           nested continues
                                                nested completes -> runner erases
```

Alternative rejected: interpreter self-ownership for detached runs. It creates a
reference cycle and makes cancellation/observability harder.

### D6 — Failure classification

| Failure | Status |
|---|---|
| `namespace` not `embedded` | `UNIMPLEMENTED` |
| `return` declared | `UNIMPLEMENTED` |
| nesting limit exceeded | `UNIMPLEMENTED` |
| `input` missing or not a string | `MALFORMED_REQUEST` |
| embedded document does not parse | `MALFORMED_REQUEST` |
| embedded `evaluate.mode` unsupported | `MALFORMED_REQUEST` |
| embedded child validation fails | `MALFORMED_REQUEST` (or its recorded status) |
| nested start fails after validation | `INTERNAL` |
| awaited child runtime failure | child's status |

Messages name the parent run step and, when available, the embedded `name`.

### D7 — No parser, SDK, config, or wire changes

The parser already carries the fields. The embedded definition is an opaque
string in `WorkflowProcess::input_`, so no model change is required. The
handler's public `HandleTask` contract, the task wire format, and the scheduler
path are untouched.

## Risks / Trade-offs

- **Runner/interpreter lifetime cycles** → nested interpreters hold only a weak
  parent reference; the runner is owned by the handler and outlives every
  interpreter; cancellation is explicit rather than destructor-driven.
- **Detached runs escape `openworkflow` concurrency admission** → the subflow's
  own child tasks are still admitted; the orchestration itself is cheap. Document
  the distinction and revisit detached subflows as child tasks if operators need
  admission for them.
- **Recursive embedding can exhaust the stack** → pre-execution nesting guard.
- **Cancellation stops orchestration, not a nested run's outstanding children** →
  consistent with the existing `call` limitation; outstanding child tasks keep
  running and their outcomes are dropped. Do not claim full cancellation.
- **Double parse (validation and execution)** → documents are bounded by the
  parent artifact limit; if profiling shows it matters, cache the parsed child
  `Document` keyed by the run task during validation.
- **Embedded parse errors point into the embedded string, not the parent YAML** →
  the failure message names the parent step and embedded `name`; source-accurate
  offsets would require retaining the raw YAML node in the model.
- **A skipped `if` still validates the subflow** → intentional, matching the
  existing pre-execution pass; authors cannot hide a malformed subflow behind a
  false condition.
- **`end`/competing-fork abandonment drops outcomes but does not cancel the
  nested run** → consistent with other child work. Only whole-parent cancellation
  cancels awaited nested runs.

## Migration Plan

No persisted state, config, or wire migration. Existing documents that used
`run.workflow` failed as unsupported and now either execute (embedded profile) or
fail with a more specific `UNIMPLEMENTED`/`MALFORMED_REQUEST` error. Rollback is
reverting the interpreter, frame, runner, handler wiring, tests, and docs; no
data is retained.

## Open Questions

- The exact nesting limit (implementation-defined; a generous constant is enough).
- Whether to cache parsed embedded documents between validation and execution
  once large embedded definitions appear.
- Whether detached subflows should later become child tasks for independent
  admission and placement.
