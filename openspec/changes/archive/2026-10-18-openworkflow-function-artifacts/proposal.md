# Proposal

## Why

`openworkflow` currently collapses two roles: the task body is both the workflow definition and the invocation input. Every other function-consuming handler, such as `piped_executable`, separates the artifact named by `parameters["function"]` from the task body that the artifact consumes. Aligning `openworkflow` with that model lets a workflow definition be deployed and invoked like any other serverless function, makes the body the workflow's input data, and lets one workflow call another through the existing `call: strij` child projection.

## What Changes

- The `openworkflow` handler SHALL require `task.parameters["function"]`, resolve it through the shared `FunctionResolver`, and load the referenced artifact as the workflow document. A task without a function reference is rejected.
- `task.body` SHALL become the workflow's invocation input instead of the document text. It seeds the data chain (known in the code as the step data value, `StepDataValue`); a document-level `input.from` is evaluated with the body as its subject, and an empty body means JSON `null`.
- Pre-execution failures gain a defined classification: missing/unresolvable function reference and non-regular or oversized artifacts are malformed requests, a missing artifact is not found, and other load errors are internal failures.
- The artifact is loaded synchronously with `open`/`fstat`/`read`/`close`, as a scoped exception to the repository's `io_uring` rule. The exception is revisited when the function resolver takes over artifact materialization.
- **BREAKING**: submitting workflow YAML as the task body is no longer supported. The definition must be named through `x-strij-function` (or the corresponding `Task.parameters["function"]`), and the body is input data. There is no legacy body-as-document fallback.
- The `FunctionResolver` signature, return type, and behavior are unchanged. Widening its contract to "artifact locator", type-aware resolution, and materialization is deferred to a follow-up.

## Capabilities

### New Capabilities

- `openworkflow-function-invocation`: how an `openworkflow` task is invoked as a function: the required function reference, loading the workflow artifact, the body as invocation input, and the failure classification for resolution and loading.

### Modified Capabilities

- `openworkflow-interpreter`: the data chain is seeded from the invocation body and a document-level `input.from` is evaluated with that body as its subject; an empty body means JSON `null`.
- `task-parameters`: the well-known `function` parameter identifies the function artifact a handler consumes, which may be an executable or a workflow document, rather than only an executable.

## Impact

- **Nodeagent:** `OpenWorkflowTaskHandler`/factory gain a `FunctionResolver` dependency and a synchronous, bounded artifact loader; the interpreter's `Start` gains an invocation-input parameter.
- **Gateway and wire protocol:** unchanged. `x-strij-function` already becomes `Task.parameters["function"]`.
- **Function resolver:** unchanged in this change; the follow-up owns semantic widening and materialization.
- **Docs/tests:** the manual `curl` procedure, the workflow handler tests, and the e2e fixture must submit a definition via the function reference and pass input in the body.
- **`AGENTS.md`:** records the scoped synchronous-I/O exception and its revisit trigger.
