# Proposal

## Why

The nodeagent has the pieces to run a workflow but no assembly. `openworkflow-dsl-parser` (settled) turns workflow text into a typed model, `expression-evaluation` (settled) evaluates jq against JSON, and `child-task-submission` (settled) lets a handler submit children that run locally or get forwarded upstream. What is missing is a handler that walks the model and drives those pieces — so today a workflow document submitted to a node is inert data.

This change builds the first working slice: a workflow task handler that executes `do` tasks and dispatches `call: strij` to real child tasks. The slice is deliberately narrow so the hard parts — the execution loop, the data chain and its role as the expression subject, and the child-result unwrap — get proven against one call arm before `http`, `use.functions`, and the remaining nine task kinds are added.

## What Changes

- **New nodeagent task handler** registered as `"openworkflow"`, constructed with a `ChildTaskSubmitter` (through `TaskHandlerDeps`) and an evaluator (through its own config). The `TaskHandler::HandleTask(const task::Task&, ResultSenderPtr)` contract is unchanged.
- **BREAKING — Removal of the existing `"workflow"` reference handler.** It submits a fixed number of identical children and is not a workflow interpreter; it is superseded rather than extended. It is removed in this change, so a node configured with `workflow` must switch to `openworkflow`.
- **Explicit suspend/resume execution loop.** A workflow task is a sequence of steps that may each suspend awaiting a child result, so the handler is a resumable state machine rather than a call stack. This is the reason the existing synchronous reference handler is not the model.
- **Two-phase execution.** A `Start()` pass validates everything that does not depend on data (call targets, `with` key sets, requirement values, expression syntax) and fails before submitting any child; a `Step()` pass runs the tasks and fails mid-flight on data-dependent problems. The resulting error distinguishes "this document is malformed" from "this run was given the wrong shape".
- **A single data chain, used as the expression subject.** One value flows task to task, either `Json` or `Opaque`, and each step's expressions receive it as the jq subject `.`. This is deliberately *not* the DSL's `$context`: `$context` is a separate workflow-scope variable that only `export`/`set` write, and this slice executes neither, so `$context` is always an empty object here. Naming the subject "context" would have collapsed two different things and implied `$context` derives from the data chain, which it does not.
- **Expression policy tied to JSON.** A `${ }` expression requires a JSON subject. An expression over an opaque subject fails with a message naming the subject, not the syntax.
- **`call: strij` as the only custom call target.** The reserved call targets `http`, `openapi`, `asyncapi`, `grpc`, and `mcp` are recognized and reported as unsupported — a distinct, named error rather than an unknown-call error, so a typo is never mistaken for missing support.
- **`with` as a closed three-key projection of the child `task::Task`:** `type`, `parameters`, `requirements`. Task data stays in `input`, never in `with`. There are no reserved or magic keys inside `Task.parameters`.
- **Strict-mode expression recognition, per the Open Workflow specification.** A string is an expression if and only if it is entirely wrapped in `${ }`; every other string is literal text. The rule applies uniformly at every string position — top-level, nested in an object, nested in an array — so a nested member is evaluated exactly like a top-level one. `evaluate.mode` defaults to `strict`; `loose` is rejected as unsupported in this slice and deferred. The whole value is one expression, so `"jobs/\(.id)"` (text outside the wrapper) is rejected rather than interpolated.
- **Per-field expression policy, on top of strict mode.** `with.type` and `with.requirements` never accept expressions; `with.parameters` values accept a whole-value `${ }` and nothing else. Requirements are unsigned integers only, so scheduling inputs stay data-independent.
- **Strict `uint64` coercion for requirements,** rejecting non-integers, negatives, and overflow with a positioned error — deliberately stricter than the gateway's `ParamsOnlyRequirementsResolver`, which truncates `100M` to `100` via `std::stoull`. That truncation is a pre-existing gateway bug, tracked separately and not fixed here.
- **`Value` ⇄ JSON bridge.** `openworkflow::Value` has no JSON representation today; the change adds a bridge to the `Jv` boundary so expressions can run against DSL values and task results can be re-read by later steps. The chain is re-sniffed at every step boundary, so a child's non-JSON result downgrades the chain rather than corrupting it.

Out of scope: the reserved call targets, `use.functions` inlining, the other nine task kinds, `set`/`emit`/`switch`/`fork`/`try`, retries, timeouts, and event payloads.

## Capabilities

### New Capabilities

- `openworkflow-interpreter`: the nodeagent workflow task handler — the resumable execution loop, the `do` body, the JSON/opaque data chain used as the expression subject (and its distinction from the DSL's `$context`), the expression policy, the static/dynamic validation split, the error contract for workflow-level failure, and the result unwrap that converts a completed child into the workflow's output.
- `openworkflow-call-strij`: the `call: strij` dispatch arm — the closed `with` key set and its projection onto `task::Task`, the per-field expression and coercion policies, the `uint64` requirements contract, and the distinct unsupported-call-target error.

### Modified Capabilities

- `child-task-submission`: removes the "Workflow task handler example" requirement, since the `"workflow"` handler it mandates is deleted. The submission seam it documents — `ChildTaskSubmitter`, the per-type router, the sender-backed `RunTask` overload — is unchanged and becomes the contract the `openworkflow` handler is validated against. The router requirement's example task type is updated from `"workflow"` to `"openworkflow"`. Its settled Purpose still names "the reference workflow handler" and is left for manual cleanup at archive.

## Impact

**New code**

- `src/nodeagent/extensions/task_handlers/openworkflow/` — the handler, its factory, and its config proto, alongside the interpreter (executed steps, the resumable state machine, the `Value` ⇄ `Jv` bridge, and the `call: strij` projection).
- `src/openworkflow/value_jv.{hh,cc}` — the `Value` ⇄ JSON bridge only, which stays in the SDK because the parser's `Value` tree is the other side of it.
- `api/nodeagent/task_handlers/openworkflow/config.proto` — evaluator selection for the handler.
- `test/nodeagent/extensions/task_handlers/openworkflow/` — interpreter and handler tests, mirroring the handler.
- `test/openworkflow/value_jv_test.cc` — bridge tests, mirroring the bridge.

**Removed code**

- `src/nodeagent/extensions/task_handlers/workflow/` and its tests — the reference handler superseded by `openworkflow`.

**Touched code**

- `src/nodeagent/exe/nodeagent_framework.cc` — register the handler in the default nodeagent wiring.
- `src/common/loaders/evaluator_loader.hh` — reuse as-is; the handler needs an evaluator instance like the gateway config path does.
- `src/common/extensions/evaluators/` — read-only; the evaluator contract is used as settled.

**Upstream dependencies (unchanged contracts, new consumers)**

- `openworkflow-dsl-parser` — provides the document model.
- `expression-evaluation` — provides expression compilation and execution; its `Compile`/`Run` split is what makes the static validation pass possible.
- `child-task-submission`, `node-local-scheduler`, `child-result-routing` — provide the submit path and result delivery for each child.

**Known interactions left open**

- Whether an opaque workflow input may be declared explicitly (`with.body: json`) rather than sniffed, which is the last structural question in the `with` envelope.
- Where a workflow-level failure is surfaced. This change reports it through `ResultReceiver::DeliverError`, which today takes a bare reason with no code. A separate change, `task-result-status`, adds a `status` field to `TaskResult` so failures carry a classification the gateway can map to an HTTP status; once that lands, the workflow handler switches from a bare reason to a code without changing the interpreter.
- Whether the pre-existing `ParamsOnlyRequirementsResolver` truncation gets its own change.
