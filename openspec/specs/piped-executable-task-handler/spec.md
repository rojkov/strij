# piped-executable-task-handler

## Purpose

Defines a task handler that spawns an external binary per task, feeds it the task body on stdin, streams stdout back as chunked results, detects exit via pidfd, and logs stderr locally.

## Requirements

### Requirement: PipedExecutableTaskHandler executes a binary per task
The system SHALL provide a task handler extension registered as `piped_executable` in `Registry<TaskHandlerFactory>`. On `HandleTask`, the handler SHALL spawn a new process from the executable path in `task.parameters["function"]`, feed `task.body` to the process stdin, stream the process stdout back as non-final `TaskResult` chunks, and deliver the final result when the process exits.

#### Scenario: Handler spawns and streams stdout
- **WHEN** `HandleTask` is called with a task whose `parameters["function"]` names a binary
- **THEN** the binary SHALL be spawned with `task.body` on stdin
- **AND** its stdout SHALL be delivered as `TaskResult` chunks with `is_final=false`
- **AND** a final `TaskResult` with `is_final=true` SHALL be delivered after the process exits

#### Scenario: Empty stdout
- **WHEN** the spawned binary writes nothing to stdout and exits
- **THEN** a single final `TaskResult` with an empty body SHALL be delivered

### Requirement: Process spawning uses posix_spawn
The handler SHALL spawn the process via `posix_spawn` with file actions redirecting stdin, stdout, and stderr to pipes. Spawning SHALL be a synchronous call made directly on the event-loop thread without io_uring involvement.

#### Scenario: Spawn redirects stdio to pipes
- **WHEN** the handler spawns a process
- **THEN** the process SHALL read stdin from a pipe fed with `task.body`
- **AND** write stdout and stderr to pipes read by the handler

### Requirement: Exit detection via pidfd and io_uring poll
The handler SHALL detect process exit using a pidfd and `PreparePoll` on the pidfd. On exit, it SHALL reap the child with `waitpid(pid, status, WNOHANG)`, drain the stdout pipe non-blocking until `EAGAIN`, and then deliver the final result.

#### Scenario: Final result follows process exit
- **WHEN** the pidfd poll completes for a child
- **THEN** the handler SHALL reap the child
- **AND** drain remaining stdout non-blocking
- **AND** deliver the final `TaskResult`

### Requirement: Stderr is logged locally
The handler SHALL read the process stderr pipe and emit the bytes through the node's logging facility. Stderr SHALL NOT be forwarded to the client.

#### Scenario: Stderr is logged and not streamed
- **WHEN** the spawned binary writes to stderr
- **THEN** the bytes SHALL appear in the node's logs
- **AND** SHALL NOT be delivered through the `ResultSender`

### Requirement: Per-task child state
Each in-flight task SHALL own a per-task state object implementing `event::Completable` that holds the owned `ResultSender` (transferred by move in `HandleTask`), the pipe fds, and the pidfd. Handler instances SHALL NOT store per-connection state; handlers SHALL be shared across nodeagent connections.

#### Scenario: Concurrent tasks are isolated
- **WHEN** two tasks run concurrently through the shared handler
- **THEN** each SHALL own independent child state

### Requirement: Connection close cancels the child
When the connection bound to the task's `ResultSender` is torn down, the handler SHALL kill the in-flight child and release its state. The handler SHALL unregister the close callback once the task has completed.

#### Scenario: Client disconnect kills the child
- **WHEN** the connection closes while a child is running
- **THEN** the child SHALL be killed and the per-task state SHALL be released

#### Scenario: Completed task unregisters its close callback
- **WHEN** a task delivers its final result
- **THEN** the handler SHALL unregister the close callback for that task

### Requirement: Spawn failure delivers an empty final result
Until error surfacing is implemented, when spawning fails (e.g. the executable does not exist) the handler SHALL log the failure and deliver a single final `TaskResult` with an empty body so the HTTP connection closes.

#### Scenario: Missing executable
- **WHEN** `posix_spawn` fails for a task
- **THEN** the handler SHALL log a warning
- **AND** deliver a final `TaskResult` with an empty body

### Requirement: PipedExecutableTaskHandlerFactory
The system SHALL provide a `PipedExecutableTaskHandlerFactory` registered as `"piped_executable"` with a config proto `PipedExecutableTaskHandlerConfig`. The factory SHALL obtain the shared `FunctionResolver` from the `FactoryContext` and pass it to the handler.

#### Scenario: Factory creates a handler with the shared resolver
- **WHEN** `Create(config, context)` is called
- **THEN** a `PipedExecutableTaskHandler` using `context.FunctionResolver()` SHALL be returned

#### Scenario: Factory registers under the piped_executable name
- **WHEN** `Name()` is called on the factory
- **THEN** "piped_executable" SHALL be returned
