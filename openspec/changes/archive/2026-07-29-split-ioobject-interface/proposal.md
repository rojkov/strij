## Why

`event::IOObject` currently bundles two orthogonal concerns into a single interface: I/O completion callbacks (`HandleCompletion`) and inter-object messaging (`ProcessCommand`). As the system grows, more objects will need command handling without I/O, and every IOObject today is forced to implement `ProcessCommand` even when it's a no-op (Connection, LogFrontend, SignalMonitor). Splitting into two independent interfaces removes the forced coupling, makes intent clear, and opens the door for command-only objects.

## What Changes

- **BREAKING**: `IOObject` is removed. Two new interfaces replace it:
  - `Completable` — I/O completion callback (`HandleCompletion`)
  - `CommandHandler` — inter-object messaging (`ProcessCommand`)
- `Command::destination_` changes from `IOObject*` to `CommandHandler*`
- `Dispatcher` methods take `Completable*` for I/O operations and `CommandHandler*` for commands
- Every class that implements IOObject today is updated to implement the appropriate interface(s)

## Capabilities

### New Capabilities

- `command-handler`: defines `CommandHandler` interface and command delivery contract. Any object implementing this interface can receive async messages via the Dispatcher.

### Modified Capabilities

- `connection-io-object`: Connection no longer inherits from IOObject — implements `Completable` only. Its `ProcessCommand` no-op is removed.
- `node-directory`: Node implements both `Completable` (HandleCompletion) and `CommandHandler` (ProcessCommand). This is explicitly stated instead of the catch-all "inherits IOObject".
- `dispatcher-connect`: `PrepareConnect` signature changes from `IOObject*` to `Completable*`. Same for all `Prepare*` methods.
- `protocol-parser`: The existing requirement "parser SHALL NOT inherit from IOObject" is rephrased as "parser SHALL NOT implement Completable" (no behavioral change).

## Impact

- `include/strij/event/io_object.hh` — replaced by `completable.hh` and `command_handler.hh`
- `include/strij/event/command.hh` — `destination_` type changes to `CommandHandler*`
- `include/strij/event/dispatcher.hh` — `Prepare*` and `SubmitCommand` signatures change
- `src/core/event/dispatcher_impl.{hh,cc}` — dispatch loop updated
- `src/core/io/connection.{hh,cc}` — removes `ProcessCommand`, implements `Completable` only
- `src/core/io/tcp_listener.{hh,cc}` — implements both interfaces
- `src/core/io/node.{hh,cc}` — implements both interfaces
- `src/core/logging/log_frontend.{hh,cc}` — implements `Completable` only
- `src/core/common/signal_monitor.{hh,cc}` — implements `Completable` only
- `test/mocks/event/mocks.hh` — mock updated
- `test/core/io/gateway_test.cc` — test updated
