## Why

`ResultReceiverStorage` never cleans up receivers when connections drop. If an HTTP client disconnects before a task completes, its receiver sits in storage forever — a memory leak. Worse, if a node connection drops, receivers for all tasks routed to that node become orphaned: the HTTP clients hang indefinitely waiting for results that will never arrive. The `OutboundMailbox` already provides a `RegisterOnClose` callback mechanism (used by `ChildProcess` and `StateReporter`), but nothing uses it to clean up gateway receivers.

## What Changes

- `ResultReceiverStorage` gains a `NotifyNodeDisconnected(node_id)` method that finds all receivers for a given node, delivers errors to still-connected HTTP clients, erases the receivers, and records completions in the `ExactStateTracker`. This method is self-contained: the storage owns a `task_id → node_id` mapping populated by `put()` and cleared by `erase()`, keeping the cleanup independent of the state tracker's internal model (important for the future D10 cached/distributed state replacement).
- `GatewayHttpHandler::HandleMessage` registers an `OutboundMailbox::RegisterOnClose` callback after storing the receiver. When the HTTP client drops, the callback removes the receiver from storage and records completion in the state tracker. This handles the case where the client disconnects before the node sends results.
- `Node` gains references to `ResultReceiverStorage` and `ExactStateTracker`. On connection creation it registers a mailbox close callback that calls `storage.NotifyNodeDisconnected(node_id)`. When a node connection drops, all orphaned receivers for that node are cleaned up in one shot.
- `ExactStateTracker` is unchanged — it remains purely for resource accounting. The receiver cleanup does not depend on its internals.

## Capabilities

### New Capabilities

- `receiver-connection-lifecycle`: Defines how `ResultReceiverStorage` cleans up receivers when HTTP client or node connections drop, using `OutboundMailbox` close callbacks. Covers both the per-receiver HTTP cleanup and the per-node batch cleanup paths.

### Modified Capabilities

- `gateway-task-bridge`: The `ResultReceiverStorage` interface gains `NotifyNodeDisconnected(node_id)` and the `put()` operation implicitly records the task→node association. The `GatewayHttpHandler` registers a close callback after storing a receiver. New scenarios for connection drop cleanup.

## Impact

- **`ResultReceiverStorage`** (`src/core/gateway/result_receiver_storage.hh`): new `node_of_task_` map, `NotifyNodeDisconnected()` method, modified `put()` to accept and store `node_id`.
- **`GatewayHttpHandler`** (`src/core/gateway/gateway_http_handler.cc`): registers mailbox close callback in `HandleMessage()`.
- **`Node`** (`src/core/gateway/node.hh`, `node.cc`): gains `storage_` and `state_tracker_` references, registers close callback in `HandleCompletion(kConnect)`.
- **`NodeDirectory` / `gateway.cc`**: `Node` construction passes storage and state_tracker references.
- **Tests**: new tests for HTTP drop cleanup, node drop cleanup, and idempotent interaction between the two paths.
