# openworkflow-function-invocation

## Purpose

Defines how an `openworkflow` task is invoked as a serverless function: the required function reference, how the workflow artifact is located and loaded, the task body as invocation input, and how invocation failures are classified and reported.

## Requirements

### Requirement: An openworkflow task requires a function reference

The `openworkflow` handler SHALL read the function reference from `task.parameters["function"]` using the shared function parameter constant. It SHALL resolve that reference through the shared `FunctionResolver`. A task without the parameter SHALL fail with `TASK_STATUS_MALFORMED_REQUEST`, and a resolver error (including an empty reference) SHALL also fail with `TASK_STATUS_MALFORMED_REQUEST`. The handler SHALL NOT fall back to interpreting the task body as a workflow document.

#### Scenario: Function reference selects the workflow document

- **WHEN** an `openworkflow` task carries `parameters["function"]` naming a workflow document
- **THEN** the handler resolves the reference and executes the referenced document

#### Scenario: Missing function reference is rejected

- **WHEN** an `openworkflow` task lacks `parameters["function"]`
- **THEN** the workflow fails with `TASK_STATUS_MALFORMED_REQUEST`
- **AND** the task body is not parsed as a workflow document

#### Scenario: Resolver rejection is a malformed request

- **WHEN** the function resolver rejects the reference, including an empty reference
- **THEN** the workflow fails with `TASK_STATUS_MALFORMED_REQUEST`

### Requirement: The workflow artifact is loaded from the resolved local path

The resolved locator SHALL be treated as a local filesystem path to the workflow artifact. The handler SHALL accept only regular files. A path that does not exist SHALL fail with `TASK_STATUS_NOT_FOUND`; a non-regular file or one exceeding the loader's maximum size SHALL fail with `TASK_STATUS_MALFORMED_REQUEST`; any other load error SHALL fail with `TASK_STATUS_INTERNAL`. A load failure SHALL NOT attempt to parse the task body.

#### Scenario: Regular file is loaded and interpreted

- **WHEN** the resolved path names a readable regular file containing a workflow document
- **THEN** the handler loads its bytes as the workflow document text and interpretation begins

#### Scenario: Missing artifact is not found

- **WHEN** the resolved path does not exist
- **THEN** the workflow fails with `TASK_STATUS_NOT_FOUND`

#### Scenario: Non-regular artifact is rejected

- **WHEN** the resolved path names a directory, FIFO, device, or other non-regular file
- **THEN** the workflow fails with `TASK_STATUS_MALFORMED_REQUEST`
- **AND** the handler does not block reading the path

#### Scenario: Oversized artifact is rejected

- **WHEN** the artifact exceeds the loader's maximum size
- **THEN** the workflow fails with `TASK_STATUS_MALFORMED_REQUEST`

#### Scenario: Other load failure is internal

- **WHEN** opening or reading the artifact fails for a reason other than a missing path
- **THEN** the workflow fails with `TASK_STATUS_INTERNAL`

### Requirement: The task body is invocation input, not the document

The task body SHALL be the workflow's invocation input and SHALL NOT be parsed as the workflow document, regardless of its content. The handler SHALL pass the body bytes unchanged to the interpreter, which owns the seed rules defined by `openworkflow-interpreter`.

#### Scenario: JSON body is passed as invocation input

- **WHEN** an `openworkflow` task carries `{"input":5}` as its body
- **THEN** the interpreter receives those bytes as the invocation input

#### Scenario: A document-shaped body is still input

- **WHEN** an `openworkflow` task body itself looks like a workflow document
- **THEN** the handler still does not parse it as a document
- **AND** the document named by the function reference is the one executed

#### Scenario: Empty body is passed unchanged

- **WHEN** an `openworkflow` task has an empty body
- **THEN** the interpreter receives an empty invocation input and applies the empty-body seed rule

### Requirement: Invocation failures are reported once

An invocation that fails before interpretation SHALL deliver exactly one final `TaskResult` whose status is the classified failure and whose body names the cause. No document interpretation SHALL begin after such a failure.

#### Scenario: Failed invocation delivers one final failure

- **WHEN** an `openworkflow` task fails during reference resolution or artifact loading
- **THEN** exactly one final `TaskResult` is delivered with the classified non-OK status
- **AND** no workflow step is executed
