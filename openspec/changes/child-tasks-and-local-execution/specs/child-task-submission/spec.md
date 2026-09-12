# child-task-submission

## ADDED Requirements

### Requirement: ChildTaskSubmitter is a shared node service

The nodeagent SHALL provide a `ChildTaskSubmitter` service exposed through `NodeagentFactoryContext`. It SHALL expose a single submission method `Submit(const task::Task& child, gateway::ResultReceiverPtr receiver)`, fire-and-forget, transferring receiver ownership. Every task handed to `Submit` SHALL eventually resolve through the receiver (result or error) — the receiver never hangs. The submitter is node-global and shared by every workflow task handler; handlers hold it as a constructor dependency, never as a per-call argument.

#### Scenario: Handler obtains the submitter at construction

- **WHEN** a task handler factory's `Create(config, context)` is called and retrieves `context.ChildTaskSubmitter()`
- **THEN** the returned reference SHALL be the node-global submitter instance shared across handler instances and connections

#### Scenario: Submit transfers receiver ownership

- **WHEN** a handler calls `Submit(child, receiver)`
- **THEN** the receiver SHALL be owned by the submission path and SHALL eventually receive a result or error

### Requirement: TaskHandler interface is unchanged by child submission

`TaskHandler::HandleTask(const task::Task&, ResultSenderPtr)` SHALL retain its signature. The nodeagent SHALL NOT add submission capabilities, arguments, or a context struct to `HandleTask`. Task handlers that submit child tasks SHALL receive the `ChildTaskSubmitter` through their factory at construction only; handlers that never submit children (e.g. `echo`, `piped_executable`) SHALL remain unchanged. `RunTaskService` SHALL NOT depend on the submitter.

#### Scenario: Non-submitting handlers are untouched

- **WHEN** the `echo` and `piped_executable` handlers are built
- **THEN** their `HandleTask` signatures and constructors SHALL be unchanged

#### Scenario: RunTaskService does not know the submitter

- **WHEN** `RunTask` runs any task
- **THEN** it SHALL NOT reference or construct the submitter

### Requirement: Node-side per-type child scheduler router

The nodeagent SHALL route child-task submissions by `task.type()` through a child scheduler router mirroring the gateway's `SchedulerRouter`: a `task_type`-matched local scheduler wins; an entry with an empty `task_type` serves as the default; a child whose type matches neither SHALL be delivered an error through its receiver. The router SHALL be the single submission entry point: `ChildTaskSubmitter::Submit` routes to the router, which delegates `Schedule(child, receiver)` to the owning local scheduler.

#### Scenario: Type-matched child routes to the owning scheduler

- **WHEN** two local schedulers are configured (one matched to `"workflow"`, one default) and a child of type `"workflow"` is submitted
- **THEN** the `Schedule` call SHALL reach the `"workflow"`-matched scheduler

#### Scenario: Unmatched child falls back to the default scheduler

- **WHEN** a child of type `"render"` is submitted and only a `"workflow"`-matched scheduler plus a default are configured
- **THEN** the `Schedule` call SHALL reach the default scheduler

#### Scenario: Unmatched child without a default is rejected

- **WHEN** a child type matches no scheduler and no default is configured
- **THEN** the router SHALL deliver an error to the child's receiver

### Requirement: Local-first child execution policy

A local scheduler's child `Schedule` SHALL follow the policy *run locally if capacity allows, else forward*. On receipt of a child submission the scheduler SHALL attempt admission via the shared `AdmissionController`; on success it SHALL run the child locally with a receiver-backed result sender; on admission failure it SHALL forward the child through the node's `GatewayClient` (see `gateway-client`). The child path SHALL NOT use probe scheduling: children are local by construction and never enter a probe queue.

#### Scenario: Admitted child runs locally

- **WHEN** a child is submitted and admission succeeds
- **THEN** the child SHALL run via `RunTask` with a sender resolving to the parent's receiver
- **AND** the child's final result SHALL be delivered to the parent's receiver

#### Scenario: Unadmitted child is forwarded

- **WHEN** a child is submitted and admission fails (e.g. node capacity exhausted)
- **THEN** the child SHALL be submitted upstream through the `GatewayClient`
- **AND** the parent's receiver SHALL be kept until the remote outcome arrives

#### Scenario: Children never enter the probe queue

- **WHEN** a child is submitted to a node running the `"probe"` local scheduler with a full probe queue
- **THEN** the child SHALL be forwarded (not enqueued)
- **AND** no `kTaskProbe`, `kTaskPull`, or `kTaskGrant` frame SHALL be emitted for it

### Requirement: Sender-backed RunTask overloads

`RunTaskService` SHALL provide overloads that run an admitted task with a caller-supplied `ResultSenderPtr` instead of an `io::Connection`: one admitting variant (`RunTask(task, sender)`) and one preallocated variant (`RunTask(task, sender, AdmissionScopePtr reserved)`). The overloads SHALL own and forward the sender to the handler; the preallocated variant SHALL release the scope on the final result. The existing connection-bound overloads SHALL remain and SHALL delegate to the same shared execution path.

#### Scenario: Local child runs with a receiver-backed sender

- **WHEN** `ChildSubmissionService` admits a child and invokes `RunTask(child, RegistryResultSender)`
- **THEN** the child's handler SHALL be invoked with that sender
- **AND** results SHALL reach the parent's receiver, not a connection

#### Scenario: Preallocated child releases capacity on final result

- **WHEN** the preallocated `RunTask` variant runs a child whose handler emits its final result
- **THEN** the supplied admission scope SHALL be released exactly once

### Requirement: Workflow task handler example

The change SHALL include an example workflow task handler (e.g. `"workflow"`) whose factory retrieves the `ChildTaskSubmitter` at construction, and which, for each parent task, submits a configurable number of child tasks (unique ids via the existing `GenerateTaskId()`), registers a parent receiver per child, and aggregates the children's final bodies into the parent's result. It SHALL serve as the reference pattern and validation vehicle for the submission seam.

#### Scenario: Workflow handler fans out and aggregates

- **WHEN** a parent task of the workflow type runs and its config declares children
- **THEN** the handler SHALL submit one child per declared child with a distinct generated id
- **AND** upon delivery of every child's final result SHALL emit the aggregated parent result as the parent's final result

#### Scenario: A child error aborts the workflow

- **WHEN** any child of a workflow resolves to an error
- **THEN** the handler SHALL emit a parent error result identifying the failed child and SHALL not wait for outstanding children