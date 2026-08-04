## Why

Task delivery is today synchronous-only: the `ResultSender` handed to a `TaskHandler` is valid only for the duration of `HandleTask`, and `Connection::Write` accepts exactly one in-flight buffer (`assert(write_buf_.empty())`). Streaming task handlers (e.g. `piped_executable`, which forks a child and streams its stdout back over time) require the inverse: a handler must be able to retain its sender, call `Send()` multiple times, and be notified when the connection dies. This change builds the async delivery substrate the codebase already flags as the required next step (`task_handlers.hh`, `result_sender.hh` docstrings).

## What Changes

- **`Connection::Write` becomes queue-safe.** Replace the single `write_buf_`/`write_offset_` pair with a deque of buffers; `Write()` appends and drains FIFO through `HandleCompletion`. Concurrent `Write()` calls no longer assert.
- **New `strij::io::OutboundMailbox`.** A `shared_ptr` handle owned by `Connection`, wrapping the connection's outbound result path with lifetime tracking: `Enqueue(bytes)`, `Close()` (drops pending sends, fires on-close callbacks), and `RegisterOnClose`/`UnregisterOnClose`.
- **`ResultSender` gains lifecycle hooks.** `Send()` stays `void`; the interface adds `RegisterOnClose(std::move_only_function<void()>) -> std::size_t` and `UnregisterOnClose(std::size_t)`. `ConnectionResultSender` becomes a copyable handle into the mailbox (a `shared_ptr<OutboundMailbox>`), so handlers may retain it past `HandleTask`.
- **Async contract.** Handlers SHALL be allowed to retain the sender, call `Send()` zero or more times (marking the last result `is_final`), and receive a teardown notification. Synchronous handlers (echo) remain valid and unchanged in behavior.
- **`NodeagentTlvHandler`** obtains the sender from the connection's mailbox instead of a stack-local raw sender.
- **BREAKING** (internal): `Connection::Write` no longer asserts on concurrent writes (tests asserting the assert are updated); the `ResultSender` interface gains two pure virtual methods (all implementors updated).

## Capabilities

### New Capabilities
- `outbound-mailbox`: The `OutboundMailbox` abstraction — a connection-owned, `shared_ptr`-shared outbound queue with lifetime tracking, close semantics, and teardown callbacks.

### Modified Capabilities
- `nodeagent-task-handlers`: `ResultSender` gains `RegisterOnClose`/`UnregisterOnClose`; the delivery contract changes from synchronous-only to async-capable (retain sender, multiple `Send()`, teardown notification).
- `connection-io-object`: `Connection::Write` becomes queue-safe (deque of buffers, FIFO drain, concurrent writes allowed); `Connection` owns an `OutboundMailbox` and closes it on teardown.

## Impact

- **Code**: `src/core/io/connection.{hh,cc}` (write queue, mailbox ownership, teardown), new `src/core/io/outbound_mailbox.{hh,cc}` (or `mailbox`), `src/core/nodeagent/result_sender.{hh,cc}` (copyable handle), `src/core/nodeagent/nodeagent_tlv_handler.cc`, `src/extensions/task_handlers/task_handlers.hh` + `echo/echo_task_handler.cc` (interface docstring + new pure virtuals).
- **Tests**: `test/core/io/connection_test.cc`, `test/core/nodeagent/nodeagent_tlv_handler_test.cc`, `test/mocks/extensions/extensions_mocks.hh` (`MockResultSender`), new mailbox tests. GoogleTest via `strij_cc_test`.
- **Dependencies**: none (C++23 `std::move_only_function` already used by codebase conventions).
- **Future enablement**: this is the substrate for `piped_executable` (process streaming), which will layer header capture, Task proto params, process supervision, and streaming HTTP on top.
