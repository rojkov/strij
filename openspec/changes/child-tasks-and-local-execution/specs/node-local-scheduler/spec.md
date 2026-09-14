# node-local-scheduler

## REMOVED Requirements

### Requirement: Child-task Schedule is reserved

**Reason**: The reserved seam recorded in Phase 0 is now activated. This change implements the node's child-task `Schedule` path (`child-task-submission`); error-delivery-in-Phase-0 is replaced by real routing and execution.

**Migration**: The active semantics live in the new requirement "Child-task Schedule routes children locally or forwards them" below. The `Schedule(` facet signature is unchanged on the `Scheduler` interface — only its node-local behavior changes.

## ADDED Requirements

### Requirement: Child-task Schedule routes children locally or forwards them

A nodeagent local scheduler's `Schedule(const task::Task&, gateway::ResultReceiverPtr)` SHALL implement the child-task policy *run locally if capacity allows, else forward*: it SHALL attempt admission through the shared `AdmissionController` and on success SHALL run the child via the sender-backed `RunTask` overload with a `RegistryResultSender`; on admission failure it SHALL forward the child through the `GatewayClient`. This applies to every local scheduler declared with a local scheduling role (`task_type` or `local_default`); schedulers with no local role do not receive submissions. `probe`'s child path SHALL NOT queue or emit probe frames. A child whose type no entry claims and for which no `local_default` is declared (`nodeagent-config`) SHALL be delivered an error through its receiver.

#### Scenario: Push scheduler runs an admissible child locally

- **WHEN** `Schedule(child, receiver)` is invoked on the `push` local scheduler and admission succeeds
- **THEN** the child SHALL run via `RunTask` with a registry-backed sender
- **AND** the child's result SHALL reach the receiver

#### Scenario: Local scheduler forwards an unadmissible child

- **WHEN** `Schedule(child, receiver)` is invoked and admission fails
- **THEN** the child SHALL be submitted upstream through the `GatewayClient`
- **AND** the receiver SHALL remain pending until the remote outcome

#### Scenario: Probe scheduler never queues a child

- **WHEN** `Schedule(child, receiver)` is invoked on the `probe` local scheduler with a full probe queue
- **THEN** the child SHALL NOT be enqueued
- **AND** the child SHALL be forwarded or, if no gateway is reachable, delivered an error