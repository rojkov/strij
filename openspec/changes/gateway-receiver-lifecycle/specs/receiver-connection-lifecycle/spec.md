# receiver-connection-lifecycle

## Purpose

Defines how `ResultReceiverStorage` cleans up receivers when HTTP client or node connections drop, using `OutboundMailbox` close callbacks.

## Requirements

### Requirement: HTTP client drop cleans up the receiver

When an HTTP client connection drops, the receiver for any in-flight task submitted over that connection SHALL be removed from `ResultReceiverStorage`. The cleanup SHALL be triggered by an `OutboundMailbox::RegisterOnClose` callback registered by `GatewayHttpHandler` at task submission time.

#### Scenario: Receiver removed when HTTP client disconnects before result

- **WHEN** a task is submitted via an HTTP connection and the HTTP client disconnects before any result arrives
- **THEN** the close callback SHALL remove the receiver from `ResultReceiverStorage` keyed by the task's ID
- **AND** the `ExactStateTracker` SHALL record completion for that task

#### Scenario: Late result for a cleaned-up receiver is dropped

- **WHEN** a node sends a result for a task whose receiver was already removed by the HTTP close callback
- **THEN** the gateway SHALL log a warning and discard the result frame
- **AND** the HTTP client SHALL NOT receive any data

#### Scenario: Idempotent cleanup on double-erase

- **WHEN** the HTTP close callback fires for a task whose receiver was already erased by a final result or rejection
- **THEN** the erase SHALL be a no-op
- **AND** no error SHALL be raised

### Requirement: Node connection drop cleans up all orphaned receivers

When a node connection drops, all receivers for tasks that were routed to that node SHALL be removed from `ResultReceiverStorage`. The cleanup SHALL be triggered by an `OutboundMailbox::RegisterOnClose` callback registered by `Node` at connection creation time.

#### Scenario: All receivers for a disconnected node are cleaned up

- **WHEN** a node connection drops and there are three in-flight tasks routed to that node
- **THEN** all three receivers SHALL be removed from `ResultReceiverStorage`
- **AND** `ExactStateTracker::RecordCompletion` SHALL be called for each task

#### Scenario: Error delivered to still-connected HTTP clients

- **WHEN** a node connection drops and an HTTP client for one of the orphaned tasks is still connected
- **THEN** `ResultReceiver::DeliverError("node disconnected")` SHALL be called for that receiver before it is erased
- **AND** the HTTP client SHALL receive a 503 Service Unavailable response (or see the connection close if streaming was in progress)

#### Scenario: No receiver found for a node's task

- **WHEN** `NotifyNodeDisconnected` is called for a node and a task's receiver was already erased (e.g., by a prior final result)
- **THEN** the cleanup SHALL skip that task without error

#### Scenario: No tasks for the node

- **WHEN** `NotifyNodeDisconnected` is called for a node with no in-flight tasks
- **THEN** the method SHALL be a no-op

### Requirement: `NotifyNodeDisconnected` is self-contained

The `NotifyNodeDisconnected(node_id)` method SHALL own a `task_id → node_id` mapping populated by `Put()` and cleared by `Erase()`. It SHALL NOT depend on `ExactStateTracker` internals for finding tasks by node, ensuring the cleanup survives future state model replacements.

#### Scenario: Task-to-node mapping populated on put

- **WHEN** `Put("task_1", receiver, "node_A")` is called
- **THEN** `NotifyNodeDisconnected("node_A")` SHALL find `task_1`

#### Scenario: Task-to-node mapping cleared on erase

- **WHEN** `Erase("task_1")` is called after `Put("task_1", receiver, "node_A")`
- **THEN** `NotifyNodeDisconnected("node_A")` SHALL NOT find `task_1`
