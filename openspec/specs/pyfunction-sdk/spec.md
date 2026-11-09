# pyfunction-sdk Specification

## Purpose

Provides the Python `strij` package: a worker runtime for `@strij.remote` calls and a client runtime that submits pyfunction tasks over the gateway HTTP endpoint.

## Requirements

### Requirement: Package surface and runtime selection

The `strij` package SHALL expose `init`, `remote`, `RemoteRef`, and `RemoteError`. A worker bootstrap SHALL install a pipe-backed runtime; `strij.init(gateway_url)` SHALL install an HTTP-backed client runtime. Calling `.remote()` with no configured runtime SHALL raise a clear error.

#### Scenario: Worker runtime is installed by the bootstrap

- **GIVEN** a Python worker process started by the pyfunction handler
- **WHEN** the worker bootstrap imports `strij`
- **THEN** a pipe-backed runtime SHALL be available
- **AND** `@strij.remote` calls SHALL submit through that runtime

#### Scenario: Client runtime is installed explicitly

- **GIVEN** a Python process that calls `strij.init("http://gateway:8080")`
- **WHEN** a remote function calls `.remote()`
- **THEN** the call SHALL use the HTTP client runtime

#### Scenario: Missing runtime is reported

- **GIVEN** a Python process with no worker bootstrap and no `strij.init`
- **WHEN** a remote function calls `.remote()`
- **THEN** a clear "no runtime configured" error SHALL be raised

### Requirement: Remote decorator forms

`@strij.remote` and `@strij.remote(requirements=..., timeout=...)` SHALL decorate a Python callable. A normal call SHALL execute locally; `.remote(*args, **kwargs)` SHALL submit the call and return a `RemoteRef`.

#### Scenario: Plain decorator submits a remote call

- **GIVEN** a function decorated with `@strij.remote`
- **WHEN** the caller invokes `.remote(1, 2)`
- **THEN** a child task SHALL be submitted
- **AND** a `RemoteRef` SHALL be returned

#### Scenario: Configured decorator carries requirements

- **GIVEN** a function decorated with `@strij.remote(requirements={"cpu": 1})`
- **WHEN** the caller invokes `.remote(...)`
- **THEN** the submitted task SHALL carry the declared requirements

#### Scenario: Normal call executes locally

- **GIVEN** a function decorated with `@strij.remote`
- **WHEN** the caller invokes the function directly without `.remote()`
- **THEN** the function SHALL run in the current process

### Requirement: RemoteRef lifecycle

`RemoteRef.get(timeout=None)` SHALL return the remote value or raise. Repeated calls SHALL be idempotent: a resolved ref returns the same value; a failed ref raises the same error. A timeout SHALL raise a timeout error without cancelling the remote task.

#### Scenario: Get returns the remote value

- **GIVEN** a submitted remote call that completes successfully
- **WHEN** the caller calls `.get()`
- **THEN** the decoded result SHALL be returned

#### Scenario: Repeated get is idempotent

- **GIVEN** a ref that has already resolved
- **WHEN** the caller calls `.get()` again
- **THEN** the same value SHALL be returned without resubmitting the task

#### Scenario: Timeout does not cancel the task

- **GIVEN** a ref whose task has not completed
- **WHEN** `.get(timeout=...)` expires
- **THEN** a timeout error SHALL be raised
- **AND** the remote task SHALL continue running

### Requirement: Worker runtime transport

In a worker, `.remote()` SHALL send a remote-call frame to the handler and return a ref. The worker runtime SHALL resolve the ref from the matching result frame, converting a non-OK status into `RemoteError` with the remote traceback.

#### Scenario: Remote call is sent as a frame

- **GIVEN** a worker executing a task
- **WHEN** the task calls `.remote(...)`
- **THEN** a remote-call frame SHALL be written to the handler
- **AND** a ref SHALL be returned immediately

#### Scenario: Result frame resolves the ref

- **GIVEN** a pending ref in a worker
- **WHEN** the matching result frame arrives
- **THEN** the ref SHALL resolve with the decoded payload

#### Scenario: Error frame rejects the ref

- **GIVEN** a pending ref in a worker
- **WHEN** the matching result frame carries a non-OK status
- **THEN** `.get()` SHALL raise `RemoteError` with that status and the remote message

### Requirement: Client runtime transport

In a client, `strij.init(gateway_url)` SHALL configure the HTTP runtime. `.remote()` SHALL submit a pickle-mode `pyfunction` request to `/tasks/pyfunction` and return a ref without waiting for completion. `.get()` SHALL decode the response body and map a non-2xx HTTP status to `RemoteError`.

#### Scenario: Client submits a pickle-mode task

- **GIVEN** an initialized client runtime
- **WHEN** a remote function calls `.remote(...)`
- **THEN** an HTTP POST SHALL be sent to `/tasks/pyfunction`
- **AND** the body SHALL be a pickled `Invocation`

#### Scenario: Client returns the decoded result

- **GIVEN** a successful HTTP response
- **WHEN** the caller calls `.get()`
- **THEN** the response body SHALL be unpickled and returned

#### Scenario: Client maps an HTTP failure

- **GIVEN** a non-2xx HTTP response
- **WHEN** the caller calls `.get()`
- **THEN** `RemoteError` SHALL carry the status mapped from the HTTP code and the response text

### Requirement: Eager RemoteRef resolution

Before serializing an invocation, the runtime SHALL recursively resolve `RemoteRef` values in positional and keyword arguments, including inside lists, tuples, and dictionaries. A ref that belongs to another runtime or cannot be resolved SHALL raise a clear error instead of being serialized. Returning a `RemoteRef` from a task SHALL fail with a clear error.

#### Scenario: Direct ref argument is resolved

- **GIVEN** a caller holds a pending `RemoteRef`
- **WHEN** it calls `fn.remote(ref)`
- **THEN** the ref SHALL be awaited before serialization
- **AND** the callee SHALL receive the resolved value

#### Scenario: Nested refs are resolved

- **GIVEN** a list or dictionary containing `RemoteRef` values
- **WHEN** the caller passes it to `.remote(...)`
- **THEN** every contained ref SHALL be resolved before serialization

#### Scenario: Foreign ref is rejected

- **GIVEN** a `RemoteRef` created by a different runtime
- **WHEN** the caller passes it to `.remote(...)`
- **THEN** a clear error SHALL be raised before submission

#### Scenario: Returning a ref is rejected

- **GIVEN** a task function that returns a `RemoteRef`
- **WHEN** the worker serializes the result
- **THEN** the task SHALL fail with a clear error

### Requirement: Versioned invocation envelope

The pickle-mode invocation envelope SHALL be a versioned cloudpickle `Invocation` carrying the callable or code reference, positional arguments, and keyword arguments. The SDK SHALL always emit the supported version. The worker SHALL reject an envelope with an unsupported version with a clear error and `MALFORMED_REQUEST` when it decodes the invocation.

#### Scenario: Envelope carries the invocation

- **GIVEN** a caller submits a callable with arguments
- **WHEN** the invocation is serialized
- **THEN** the envelope SHALL contain the callable or code reference, arguments, and keyword arguments
- **AND** it SHALL include the supported version

#### Scenario: Unsupported version is rejected by the worker

- **GIVEN** a task body containing an envelope produced with an unsupported version
- **WHEN** the worker decodes the invocation
- **THEN** the task SHALL fail with `MALFORMED_REQUEST`
- **AND** the error SHALL name the unsupported version and the supported version

### Requirement: Error and timeout semantics

`RemoteError` SHALL carry the `TaskStatus` and a message. A remote Python exception SHALL preserve the remote traceback text. `.get(timeout=...)` that expires SHALL raise a timeout error distinct from a remote failure.

#### Scenario: Remote exception preserves its traceback

- **GIVEN** a remote task that raises a Python exception
- **WHEN** the caller calls `.get()`
- **THEN** `RemoteError` SHALL include the remote traceback text
- **AND** its status SHALL be `INTERNAL`

#### Scenario: Capacity failure is distinguishable

- **GIVEN** a remote task rejected for capacity
- **WHEN** the caller calls `.get()`
- **THEN** `RemoteError` SHALL carry `CAPACITY_REFUSED`

#### Scenario: Timeout is distinguishable from failure

- **GIVEN** a `.get(timeout=...)` call that expires
- **WHEN** the timeout fires
- **THEN** a timeout error SHALL be raised
- **AND** it SHALL NOT be reported as `RemoteError`

### Requirement: Thread-safe worker submissions

The worker runtime SHALL be safe to use from multiple threads within a task: concurrent `.remote()` calls SHALL serialize frame writes, and refs SHALL resolve on the thread that awaits them.

#### Scenario: Concurrent submissions do not interleave frames

- **GIVEN** two threads in a worker submitting remote calls concurrently
- **WHEN** both write remote-call frames
- **THEN** the frames SHALL be written without interleaving

#### Scenario: Awaiting thread receives its ref result

- **GIVEN** a ref awaited on a particular thread
- **WHEN** the result frame arrives
- **THEN** that thread SHALL observe the resolved value

### Requirement: Worker bootstrap

The package SHALL provide a worker bootstrap that imports `cloudpickle`, the generated pipe-protocol modules and their `protobuf` runtime, and `strij`, installs the pipe runtime, announces readiness, and serves run and remote-result frames until shutdown. The bootstrap SHALL keep protocol bytes off stdout.

#### Scenario: Worker announces readiness

- **GIVEN** a worker process started by the handler
- **WHEN** the bootstrap has imported its dependencies and installed the runtime
- **THEN** it SHALL send a readiness frame
- **AND** it SHALL accept run frames

#### Scenario: Worker without a compatible protobuf runtime does not become ready

- **GIVEN** a worker environment with `cloudpickle` but no compatible protobuf runtime
- **WHEN** the worker bootstrap starts
- **THEN** it SHALL fail before sending the readiness frame
- **AND** the handler SHALL keep the slot unavailable instead of assigning tasks to that worker

#### Scenario: Worker serves until shutdown

- **GIVEN** a ready worker
- **WHEN** the handler sends run and remote-result frames
- **THEN** the bootstrap SHALL dispatch them to the runtime
- **AND** it SHALL exit cleanly on a shutdown frame

#### Scenario: User output does not corrupt the protocol

- **GIVEN** a task that calls `print()`
- **WHEN** the worker writes to stdout
- **THEN** the output SHALL go to the worker's log stream
- **AND** the protocol stream SHALL remain intact
