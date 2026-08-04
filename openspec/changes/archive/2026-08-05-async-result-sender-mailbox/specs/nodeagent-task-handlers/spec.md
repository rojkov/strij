## MODIFIED Requirements

### Requirement: TaskHandler interface
The system SHALL define an abstract `TaskHandler` interface in the `strij::extensions` namespace with a single pure virtual `HandleTask(const strij::task::Task& task, ResultSender& sender)`. The handler SHALL process the task and deliver zero or more `TaskResult` messages to the originating connection through the provided `ResultSender`. A handler SHALL be permitted to retain the `sender` past the `HandleTask` call and call `Send()` zero or more times asynchronously, marking the final result with `is_final = true`. The `sender` SHALL remain valid until the connection is torn down; sends made after teardown SHALL be dropped. A synchronous handler MAY deliver its result before `HandleTask` returns.

#### Scenario: Synchronous handler delivers before returning
- **WHEN** `HandleTask(task, sender)` is called on a synchronous task handler
- **THEN** the handler SHALL process the `task`
- **AND** deliver a `TaskResult` via `sender.Send(result)` before returning

#### Scenario: Asynchronous handler retains the sender and sends multiple times
- **WHEN** an async handler calls `HandleTask` and later calls `sender.Send(result1)` then `sender.Send(result2)`
- **THEN** both results SHALL be delivered to the originating connection in order
- **AND** the final result SHALL mark `is_final = true`

## ADDED Requirements

### Requirement: ResultSender lifecycle hooks
The `ResultSender` interface SHALL provide `Send(TaskResult)` (returning void, unchanged), plus `RegisterOnClose(std::move_only_function<void()>) -> std::size_t` and `UnregisterOnClose(std::size_t)`. A handler SHALL use `RegisterOnClose` to be notified when the connection bound to the sender is torn down, and SHALL call `UnregisterOnClose` when the callback is no longer needed (e.g. after delivering the final result). The concrete sender `ConnectionResultSender` SHALL be a copyable handle into the connection's `OutboundMailbox` and SHALL be valid to retain past `HandleTask`.

#### Scenario: Handler registers a teardown callback
- **WHEN** a handler calls `RegisterOnClose(cb)` on a retained sender
- **THEN** the returned token SHALL be usable with `UnregisterOnClose`
- **AND** `cb` SHALL be invoked when the connection is torn down

#### Scenario: Handler unregisters the callback
- **WHEN** a handler calls `UnregisterOnClose(token)` after its task completes
- **THEN** the callback SHALL NOT fire on later connection teardown

#### Scenario: ConnectionResultSender is retainable
- **WHEN** `NodeagentTlvHandler` passes a sender to `HandleTask`
- **THEN** the handler SHALL be able to copy and retain the sender past the `HandleTask` call
