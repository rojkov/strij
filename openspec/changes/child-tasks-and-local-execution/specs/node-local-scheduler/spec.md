# node-local-scheduler

## REMOVED Requirements

### Requirement: Child-task Schedule is reserved

**Reason**: The reserved seam recorded in Phase 0 is now activated. This change implements the node's child-task `Schedule` path (`child-task-submission`); error-delivery-in-Phase-0 is replaced by real routing and execution.

**Migration**: The active semantics live in the new requirement "Child-task Schedule routes children locally or forwards them" below. The `Schedule(` facet signature is unchanged on the `Scheduler` interface — only its node-local behavior changes.

## ADDED Requirements

### Requirement: The bundled default scheduler owns the child-task policy

The nodeagent SHALL provide a bundled `default` local scheduler (factory name `"default"`) that owns the node's child-task policy. Its `Schedule(const task::Task&, gateway::ResultReceiverPtr)` SHALL implement *run locally if capacity allows, else forward*: it SHALL first register the child's receiver in its own `LocalResultReceiverStorage` under the child's id, then attempt admission through the shared `AdmissionController`; on success it SHALL run the child via the sender-backed `RunTask` overload with a `StorageResultSender` bound to that storage; on admission failure (or when no local handler claims the child's type) it SHALL forward the child through the node's `ChildForwarder` (`GatewayClient`). When forwarding is unavailable or fails, it SHALL deliver an error through the registered receiver and erase the entry — the parent never hangs. The `default` scheduler SHALL advertise no wire protocol (`RequiredProtocol()` empty) and SHALL own the node's child-outcome frame types (`kResult`, `kTaskRejected`; see `child-result-routing`). Its config message (`DefaultSchedulerConfig`) SHALL be empty.

#### Scenario: Default scheduler runs an admissible child locally

- **WHEN** `Schedule(child, receiver)` is invoked on the `default` scheduler, a local handler claims the type, and admission succeeds
- **THEN** the child SHALL run via `RunTask` with a storage-backed sender
- **AND** the child's result SHALL reach the receiver
- **AND** the storage entry SHALL be erased on the final result

#### Scenario: Default scheduler forwards an unadmissible child

- **WHEN** `Schedule(child, receiver)` is invoked, admission fails, and a live forward path exists
- **THEN** the child SHALL be submitted upstream through the forwarder
- **AND** the receiver SHALL remain registered under the child's id until the remote outcome arrives

#### Scenario: Default scheduler errors an unsatisfiable child

- **WHEN** a child's type has no local handler, admission fails, and no live forward path exists (or `Forward` fails)
- **THEN** the receiver SHALL be delivered an error
- **AND** the storage entry SHALL be erased

### Requirement: Push and probe Schedule facets are unimplemented

The `push` and `probe` local schedulers SHALL be pure wire-protocol counterparts: their `Schedule(const task::Task&, gateway::ResultReceiverPtr)` SHALL be **Unimplemented**. Each SHALL log a warning and deliver an error through the receiver (honoring the resolve contract — the parent never hangs). They SHALL NOT register receivers, admit, queue, or forward. `probe`'s child path SHALL NOT emit probe frames. All locally-originated children SHALL route through the bundled `default` scheduler (the single local authority).

#### Scenario: Push Schedule delivers an unimplemented error

- **WHEN** `Schedule(child, receiver)` is invoked on the `push` local scheduler
- **THEN** the scheduler SHALL log an "unimplemented" warning
- **AND** the receiver SHALL be delivered an error referencing the unimplemented path
- **AND** nothing SHALL be admitted, queued, or written to any connection

#### Scenario: Probe Schedule delivers an unimplemented error

- **WHEN** `Schedule(child, receiver)` is invoked on the `probe` local scheduler with a full probe queue
- **THEN** the child SHALL NOT be enqueued or pulled
- **AND** the receiver SHALL be delivered an error
- **AND** no `kTaskProbe`, `kTaskPull`, or `kTaskGrant` frame SHALL be emitted