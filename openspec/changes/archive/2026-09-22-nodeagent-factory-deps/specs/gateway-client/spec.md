# Spec Delta

## MODIFIED Requirements

### Requirement: GatewayClient egress facility

The nodeagent SHALL provide a `GatewayClient` egress facility implementing the `ChildTaskForwarder` port. The `ChildTaskForwarder` contract SHALL be declared under the public include surface so extension authors can name it, and the facility SHALL be exposed to local schedulers through the `NodeSchedulerDeps` bundle. The port SHALL expose `Forward(const task::Task& task) -> absl::Status`, which forwards a child task to a gateway by writing an upstream `kTaskSubmission` frame carrying the serialized `Task` (id, type, body, parameters, requirements, deps). `Forward` SHALL NOT take ownership of or retain the child's result receiver: the receiver stays registered in the node's `LocalResultReceiverStorage` (`child-result-routing`), and the child's outcomes return through that entry. `Forward` SHALL return `OkStatus` once the frame is written (the outcome resolving asynchronously) and a non-Ok status when forwarding is impossible (no live connection registered). The `ChildTaskForwarder` port is consumed only by the node's local-authority scheduler; it is not a handler-facing capability.

#### Scenario: Forward writes an upstream submission frame

- **WHEN** the local-authority scheduler calls `Forward(child)` on the `ChildTaskForwarder` with a live gateway connection
- **THEN** a `kTaskSubmission` frame SHALL be written to that connection carrying the serialized child task

#### Scenario: Forward leaves the receiver in the local storage

- **WHEN** `Forward(child)` is called while the child's receiver is registered in `LocalResultReceiverStorage`
- **THEN** the `ChildTaskForwarder` SHALL NOT take ownership of or retain the receiver
- **AND** the child's outcome SHALL resolve through the registered storage entry

#### Scenario: Forward without a live connection returns an error

- **WHEN** `Forward(child)` is called and no live gateway connection is registered
- **THEN** `Forward` SHALL return a non-Ok status and SHALL NOT retain the child
- **AND** the caller SHALL deliver the error through the child's receiver and erase the storage entry
