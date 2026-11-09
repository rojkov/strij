# Spec Delta

## Purpose

Runs Python functions as Strij tasks in persistent worker processes, supports reference/JSON and pickle invocation modes, and routes nested remote calls through the node's existing child-task scheduler.

## ADDED Requirements

### Requirement: Handler registration and slot capacity

The system SHALL provide a bundled task handler registered as `pyfunction`. Its configuration SHALL declare `capacity.concurrency` as the number of worker processes and the maximum number of in-flight `pyfunction` tasks. Every admitted task SHALL own exactly one slot for its lifetime, and the handler SHALL NOT maintain a task queue. A configured concurrency of zero SHALL be rejected at node startup.

#### Scenario: Handler registers under the pyfunction name

- **GIVEN** a node configuration with a `pyfunction` handler entry
- **WHEN** the node builds its capabilities and loads task handlers
- **THEN** the handler SHALL be registered for task type `pyfunction`

#### Scenario: Concurrency defines the slot count

- **GIVEN** a `pyfunction` handler configured with `capacity.concurrency = 3`
- **WHEN** the node advertises its handler capabilities
- **THEN** the advertised concurrency SHALL be 3
- **AND** the handler SHALL own three worker slots

#### Scenario: Zero concurrency is rejected

- **GIVEN** a `pyfunction` handler configured with `capacity.concurrency = 0`
- **WHEN** the node starts
- **THEN** startup SHALL fail with an error naming the `pyfunction` handler

### Requirement: Persistent worker process per slot

Each slot SHALL own one persistent Python worker process. The worker SHALL be spawned with `posix_spawn`, communicate over dedicated pipes, send stdout and stderr to node logs, and the handler SHALL detect exit through a pidfd. A worker SHALL be reused for successive tasks and SHALL NOT be restarted per task.

#### Scenario: Worker is reused across tasks

- **GIVEN** an idle worker in a slot
- **WHEN** a task is assigned to the slot and completes
- **THEN** the worker SHALL return to idle
- **AND** the next assigned task SHALL run in the same worker process

#### Scenario: Worker stderr goes to logs

- **GIVEN** a worker executing a task that writes to stderr
- **WHEN** the handler reads the worker's stderr pipe
- **THEN** the bytes SHALL appear in the node's logs
- **AND** they SHALL NOT be delivered as task result data

#### Scenario: Worker exit is detected through the pidfd

- **GIVEN** a running worker process
- **WHEN** the process exits
- **THEN** the handler's pidfd poll SHALL complete
- **AND** the handler SHALL reap the process and enter the crash path

### Requirement: Slot assignment without a queue

On `HandleTask`, the handler SHALL assign the task to an available slot. A slot whose worker is still starting SHALL hold its assigned task until the worker is ready. If no worker can be assigned, the handler SHALL resolve the task with `UNAVAILABLE` rather than queueing or dropping it.

#### Scenario: Task assigned to an idle worker

- **GIVEN** an idle worker and a pending `pyfunction` task
- **WHEN** `HandleTask` runs
- **THEN** the task SHALL be assigned to that worker immediately

#### Scenario: Starting worker holds its task

- **GIVEN** a slot whose worker has been spawned but has not completed the readiness handshake
- **WHEN** a task is assigned to that slot
- **THEN** the task SHALL wait in the slot without being moved to another slot
- **AND** the task SHALL run once the worker reports ready

#### Scenario: No assignable worker

- **GIVEN** every worker is dead, restarting, or otherwise unavailable
- **WHEN** `HandleTask` runs
- **THEN** the handler SHALL deliver a final result with `UNAVAILABLE`
- **AND** the task SHALL NOT be queued

### Requirement: Exactly one terminal outcome per task

Every `HandleTask` invocation SHALL eventually deliver exactly one final `TaskResult` to its `ResultSender`, either success or a non-OK status. The handler SHALL NOT deliver more than one final result and SHALL NOT retain the sender after the outcome.

#### Scenario: Successful task sends one final result

- **GIVEN** a task that the worker completes successfully
- **WHEN** the result frame arrives
- **THEN** the handler SHALL send exactly one `TaskResult` with `is_final = true`
- **AND** the sender SHALL be released

#### Scenario: Failed task sends one final error

- **GIVEN** a task that fails before or during execution
- **WHEN** the handler resolves the failure
- **THEN** the handler SHALL send exactly one `TaskResult` with a non-OK status and `is_final = true`
- **AND** no further result SHALL be sent for that task

### Requirement: Reference/JSON invocation mode

When `Task.parameters["function"]` is present, the handler SHALL use reference/JSON mode. `embedded:<entry>` SHALL read Python source from `python_code` in the JSON body, execute it, and resolve `<entry>`; `module:<module>:<qualname>` SHALL import and resolve the named callable. The body and result SHALL be JSON.

#### Scenario: Embedded source runs the named entrypoint

- **GIVEN** a task with `parameters["function"] = "embedded:main"` and a JSON body containing `python_code`
- **WHEN** the worker executes the task
- **THEN** the worker SHALL execute the embedded source, resolve `main`, and call it
- **AND** the returned value SHALL be delivered as JSON

#### Scenario: Module reference resolves an importable callable

- **GIVEN** a task with `parameters["function"] = "module:example.tasks:main"`
- **WHEN** the worker executes the task
- **THEN** the worker SHALL import `example.tasks` and call `main`

#### Scenario: Malformed reference body is rejected

- **GIVEN** a reference-mode task whose body is not valid JSON
- **WHEN** the worker receives the task
- **THEN** the task SHALL fail with `MALFORMED_REQUEST`

#### Scenario: Unresolvable code reference is reported as not found

- **GIVEN** a reference-mode task naming a module or entrypoint that cannot be resolved
- **WHEN** the worker executes the task
- **THEN** the task SHALL fail with `NOT_FOUND`

#### Scenario: Non-JSON result is rejected

- **GIVEN** a reference-mode task whose function returns a value that cannot be serialized as JSON
- **WHEN** the worker finishes execution
- **THEN** the task SHALL fail with `INTERNAL`

### Requirement: Reference/JSON argument binding

In reference/JSON mode, the function arguments SHALL be derived from the body after removing `python_code`: `args`/`kwargs` take precedence, otherwise `input` is the single positional argument, otherwise the whole body is passed. Supplying `input` together with `args` or `kwargs` SHALL be rejected as malformed.

#### Scenario: Input field is the single argument

- **GIVEN** a body containing `input: 2` and `python_code`
- **WHEN** the worker calls the entrypoint
- **THEN** the entrypoint SHALL receive the value `2`

#### Scenario: Args and kwargs are passed through

- **GIVEN** a body containing `args` and `kwargs`
- **WHEN** the worker calls the entrypoint
- **THEN** the entrypoint SHALL receive the positional and keyword arguments from those fields

#### Scenario: Whole body is the argument

- **GIVEN** a body with no `input`, `args`, or `kwargs` keys
- **WHEN** the worker calls the entrypoint
- **THEN** the entrypoint SHALL receive the whole body as its single positional argument

#### Scenario: Mixing input with args or kwargs is rejected

- **GIVEN** a body containing both `input` and `args`
- **WHEN** the worker validates the invocation
- **THEN** the task SHALL fail with `MALFORMED_REQUEST`

### Requirement: Pickle invocation mode

When `parameters["function"]` is absent, the handler SHALL use pickle mode. The body SHALL be a versioned cloudpickle `Invocation` carrying the callable or code reference, positional arguments, and keyword arguments. The result SHALL be a cloudpickle-serialized value. An unsupported version or unpicklable body SHALL be `MALFORMED_REQUEST`.

#### Scenario: Pickled invocation runs the callable

- **GIVEN** a task with no `function` parameter and a valid pickled `Invocation`
- **WHEN** the worker executes the task
- **THEN** the worker SHALL call the invocation's callable with its arguments
- **AND** the returned value SHALL be delivered as a pickled result

#### Scenario: Unsupported envelope version is rejected

- **GIVEN** a pickled `Invocation` with an unsupported version
- **WHEN** the worker receives the task
- **THEN** the task SHALL fail with `MALFORMED_REQUEST`

#### Scenario: Unpicklable body is rejected

- **GIVEN** a task body that is not a valid pickled `Invocation`
- **WHEN** the worker receives the task
- **THEN** the task SHALL fail with `MALFORMED_REQUEST`

### Requirement: Remote calls become child tasks

A remote call emitted by a worker SHALL be submitted through the node's `ChildTaskSubmitter` as a child task of type `pyfunction`, carrying the serialized invocation and declared requirements. The handler SHALL deliver the child's result or error back to the originating worker and SHALL NOT use a private nested execution path.

#### Scenario: Remote call is submitted as a child task

- **GIVEN** a worker executing a task that calls `fn.remote(...)`
- **WHEN** the handler receives the remote-call frame
- **THEN** it SHALL call `ChildTaskSubmitter::Submit` with a `pyfunction` child task
- **AND** it SHALL retain a receiver for the child's outcome

#### Scenario: Child result returns to the originating worker

- **GIVEN** a child task submitted from a worker's remote call
- **WHEN** the child produces a result
- **THEN** the handler SHALL send the result to the originating worker
- **AND** the worker's pending call SHALL resolve

#### Scenario: Child error returns to the originating worker

- **GIVEN** a child task submitted from a worker's remote call
- **WHEN** the child fails or is rejected
- **THEN** the handler SHALL send the error and status to the originating worker
- **AND** the worker's pending call SHALL reject

### Requirement: Failure status mapping

The handler SHALL map failures to `TaskStatus`: malformed invocation or unsupported version to `MALFORMED_REQUEST`; unresolvable code reference to `NOT_FOUND`; Python exception to `INTERNAL`; no assignable worker to `UNAVAILABLE`; timeout to `DEADLINE_EXCEEDED`; admission or no eligible node to `CAPACITY_REFUSED`.

#### Scenario: Python exception maps to internal

- **GIVEN** a worker executing a function that raises an exception
- **WHEN** the worker reports the failure
- **THEN** the final result SHALL carry `INTERNAL` and the exception text

#### Scenario: No eligible node maps to capacity refused

- **GIVEN** a nested remote call that no node can admit
- **WHEN** the child outcome reaches the handler
- **THEN** the worker's pending call SHALL fail with `CAPACITY_REFUSED`

#### Scenario: No assignable worker maps to unavailable

- **GIVEN** no worker that can accept a task
- **WHEN** the handler resolves the task
- **THEN** the final result SHALL carry `UNAVAILABLE`

### Requirement: Timeouts

The handler SHALL support a startup timeout, an optional task timeout where zero disables it, and a remote-call timeout. A task timeout SHALL kill the worker and deliver `DEADLINE_EXCEEDED`; a remote-call timeout SHALL resolve the worker's pending call with an error.

#### Scenario: Task timeout kills the worker

- **GIVEN** a configured task timeout and a task that exceeds it
- **WHEN** the timeout expires
- **THEN** the handler SHALL kill the worker
- **AND** the task SHALL receive a final `DEADLINE_EXCEEDED` result

#### Scenario: Remote-call timeout releases the worker

- **GIVEN** a configured remote-call timeout and a child that does not resolve
- **WHEN** the timeout expires
- **THEN** the worker's pending call SHALL fail with an error
- **AND** the worker SHALL be able to continue

#### Scenario: Startup timeout retires the worker

- **GIVEN** a worker that does not complete the readiness handshake within the startup timeout
- **WHEN** the timeout expires
- **THEN** the handler SHALL kill the worker and attempt a restart

### Requirement: Worker crash handling

On unexpected worker exit, the handler SHALL fail the in-flight task with `INTERNAL`, drop outstanding remote-call receivers, and respawn the worker. It SHALL NOT retry the failed task automatically.

#### Scenario: In-flight task fails on crash

- **GIVEN** a worker executing a task
- **WHEN** the worker exits unexpectedly
- **THEN** the task SHALL receive a final `INTERNAL` result

#### Scenario: Outstanding remote calls are dropped

- **GIVEN** a worker with pending remote calls
- **WHEN** the worker exits unexpectedly
- **THEN** the handler SHALL drop the corresponding child receivers
- **AND** it SHALL NOT deliver their outcomes to the dead worker

#### Scenario: Worker is respawned without retrying

- **GIVEN** a crashed worker
- **WHEN** the handler recovers the slot
- **THEN** it SHALL spawn a replacement worker
- **AND** it SHALL NOT resubmit the failed task automatically

### Requirement: Client disconnect cancels the task

When the connection bound to a task's result sender is torn down, the handler SHALL kill the task's worker, drop the result, and free the slot. It SHALL unregister the close callback when the task reaches a terminal outcome.

#### Scenario: Disconnect kills the worker

- **GIVEN** a running task whose client connection closes
- **WHEN** the sender's close callback fires
- **THEN** the handler SHALL kill the task's worker
- **AND** the slot SHALL become available after the worker exits

#### Scenario: Terminal outcome unregisters the callback

- **GIVEN** a task that has delivered its final result
- **WHEN** the connection later closes
- **THEN** the close callback SHALL NOT fire for that task

### Requirement: Bounded worker protocol

The handler and worker SHALL exchange length-prefixed protobuf frames over dedicated pipes with a version handshake and a configured maximum frame size. A frame exceeding the bound SHALL terminate the worker. Invocation payload bytes SHALL remain opaque to the handler.

#### Scenario: Version mismatch is rejected

- **GIVEN** a worker that reports an unsupported protocol version
- **WHEN** the handler receives the handshake
- **THEN** the handler SHALL reject the worker and treat the slot as failed

#### Scenario: Oversized frame terminates the worker

- **GIVEN** a frame whose declared length exceeds the configured maximum
- **WHEN** the handler parses it
- **THEN** the handler SHALL terminate the worker

#### Scenario: Payload remains opaque

- **GIVEN** a pickle-mode invocation
- **WHEN** the handler moves the body between the node and the worker
- **THEN** the handler SHALL NOT inspect or modify the payload bytes

### Requirement: Configuration validation

`PyFunctionTaskHandlerConfig` SHALL declare capacity, python executable, worker module or executable, optional python paths, startup/task/remote-call timeouts, and maximum frame bytes. Invalid values SHALL fail node startup with an error naming the handler.

#### Scenario: Valid configuration starts the handler

- **GIVEN** a valid `pyfunction` configuration
- **WHEN** the node starts
- **THEN** the handler SHALL be created with the declared settings

#### Scenario: Missing worker target is rejected

- **GIVEN** a configuration with neither a worker module nor a worker executable
- **WHEN** the node starts
- **THEN** startup SHALL fail with an error naming the `pyfunction` handler

#### Scenario: Negative timeout is rejected

- **GIVEN** a configuration with a negative timeout
- **WHEN** the node starts
- **THEN** startup SHALL fail with an error naming the `pyfunction` handler
