# node-local-scheduler

## Purpose

Defines the nodeagent-side local scheduler extension category: how local scheduling extensions are registered, configured, and constructed per `NodeAgentConfig.schedulers` entry, their `Scheduler`/`CommandHandler` roles, the shared core-owned `RunTask` execution path, the frame-dispatcher role of `NodeagentTlvHandler`, and the bundled `default` scheduler that owns the node's child-task policy.

## Requirements

### Requirement: Node local scheduler extension interface

The system SHALL define nodeagent-side local scheduler extensions implementing the same `Scheduler` interface as gateway schedulers: `Schedule(const task::Task&, ResultReceiverPtr)` (fire-and-forget), `RequiredProtocol() -> std::string_view`, `HandleFrame(io::TlvFrame, io::Connection&)` (default no-op), and `HandledFrameTypes() -> std::span<const uint8_t>` (default empty). A `NodeSchedulerFactory` SHALL be registered in `Registry<NodeSchedulerFactory>` via the existing `REGISTER_FACTORY` macros, following the `ExtensionConfig` configuration pattern, and SHALL be created with a `NodeSchedulerDeps` bundle exposing the event dispatcher, the shared `RunTask` service, the shared `AdmissionController`, the `ChildTaskForwarder`, and the `DataDependencyFetcherRouter`. The nodeagent SHALL construct one local scheduler instance per `NodeAgentConfig.schedulers` entry; each instance implements a single scheduling protocol (its `RequiredProtocol()`), so a node can serve gateways running different schedulers by hosting one instance per protocol. All instances SHALL share the node's `AdmissionController` and `RunTask` service; frame dispatch SHALL be by TLV type-id, so instance ownership never overlaps.

#### Scenario: Node scheduler factory is registered and created

- **WHEN** a `NodeSchedulerFactory` with name `"push"` is registered and looked up in `Registry<NodeSchedulerFactory>`
- **THEN** the factory SHALL be found
- **AND** `Create(config, deps)` SHALL return a `std::unique_ptr<Scheduler>`

#### Scenario: Scheduler factories do not see handler-only services

- **WHEN** a node scheduler factory receives its `NodeSchedulerDeps`
- **THEN** the bundle SHALL NOT expose `FunctionResolver` or `ChildTaskSubmitter`

#### Scenario: Node scheduler declares its handled frame types

- **WHEN** the local scheduler declares `HandledFrameTypes()` containing `kTaskSubmission`
- **THEN** the nodeagent frame dispatcher SHALL route `kTaskSubmission` frames to that scheduler

### Requirement: Nodeagent scheduler CommandHandler role

A nodeagent local scheduler SHALL additionally implement `event::CommandHandler`; `ProcessCommand` SHALL be invoked on the event loop for node-internal events (e.g. a future capacity-released signal), delivered as addressed `Command` messages through the event dispatcher's command queue. Producers of internal events SHALL NOT depend on the `Scheduler` interface; they SHALL only hold the scheduler's `CommandHandler` destination and the dispatcher. The role SHALL be established now; its first consumer arrives in a later phase.

#### Scenario: Internal event reaches the scheduler

- **WHEN** a producer submits a `Command` addressed to the local scheduler on the event dispatcher
- **THEN** the scheduler's `ProcessCommand` SHALL be called on the event-loop thread with that command

#### Scenario: Producers do not depend on the Scheduler interface

- **WHEN** a producer (e.g. the admission controller) notifies the scheduler of an internal event
- **THEN** it SHALL NOT reference the `Scheduler` interface
- **AND** it SHALL reference only the scheduler's `event::CommandHandler` role

### Requirement: Nodeagent scheduler capacity-release observer

A nodeagent local scheduler SHALL be able to register as a capacity-release observer with the shared `AdmissionController` via `AdmissionController::RegisterCapacityObserver(event::CommandHandler*)`. The `AdmissionController` is shared by all local schedulers on the node; on each capacity release it SHALL submit a `CAPACITY_RELEASED` command addressed to every registered observer through the event dispatcher. The notification SHALL be node-global: capacity freed by any task (regardless of which scheduler's protocol carried it) SHALL wake every registered observer, and each observer SHALL re-verify admission itself. The registration is one-way and happens once at scheduler construction; schedulers and the controller are process-lifetime objects, so no unregistration is required.

#### Scenario: Registered observer is woken on capacity release

- **WHEN** a local scheduler has registered itself with `AdmissionController` and a task on the node completes, releasing its admitted capacity
- **THEN** the controller SHALL submit a `CAPACITY_RELEASED` command addressed to that scheduler
- **AND** the scheduler's `ProcessCommand` SHALL be invoked with that command on the event loop

#### Scenario: Release with no registered observers submits no commands

- **WHEN** `AdmissionController::Release()` is called while the observer list is empty (the only configured scheduler is `push`, which does not register)
- **THEN** no `Command` SHALL be submitted

#### Scenario: Capacity release after admission is broadcast, not dropped

- **WHEN** a `Release()` call actually frees admitted capacity (the counters decrease) and one or more observers are registered
- **THEN** each registered observer SHALL receive exactly one `CAPACITY_RELEASED` command

### Requirement: Capacity-released wakeups are pure signals

A `CAPACITY_RELEASED` command SHALL carry `args_ == nullptr`; it SHALL carry no payload describing the released capacity. The receiving scheduler SHALL NOT read `args_`; it SHALL re-derive current capacity from the `AdmissionController` (e.g. `SharedFree()`, `InFlight()`, and its own queue) inside `ProcessCommand`. A release of capacity that does not satisfy any queued probe's requirements SHALL be an idempotent no-op on the receiving side.

#### Scenario: Observer re-queries capacity instead of reading a payload

- **WHEN** a scheduler receives a `CAPACITY_RELEASED` command
- **THEN** it SHALL consult the `AdmissionController` for current capacity before acting
- **AND** it SHALL NOT act on any data carried in `args_`

#### Scenario: Spurious wakeup from unrelated capacity is a no-op

- **WHEN** a scheduler receives `CAPACITY_RELEASED` but the freed capacity does not satisfy the requirements of anything it is waiting on
- **THEN** the scheduler SHALL take no admission or forwarding action

### Requirement: RunTask service

The system SHALL provide a core-owned `RunTask` service accessible to nodeagent schedulers through `NodeagentFactoryContext`. `RunTask` SHALL take a parsed `Task` and the originating `Connection`, look up the task handler by `task.type()`, admit the task via the `AdmissionController`, and on admission run the handler with a result sender that emits `kResult` frames (and releases the admission scope on the final result). When admission fails, `RunTask` SHALL emit a `kTaskRejected` frame with the admission error. The execution path SHALL be the single shared path used by every local scheduler.

#### Scenario: RunTask executes an admitted task

- **WHEN** `RunTask` is invoked with a task whose type has a registered handler and whose admission succeeds
- **THEN** the task handler SHALL be invoked with a result sender
- **AND** the handler's results SHALL be emitted as `kResult` frames on the connection

#### Scenario: RunTask rejects an unadmissible task

- **WHEN** `RunTask` is invoked with a task whose admission fails (e.g. a pool is exhausted)
- **THEN** a `kTaskRejected` frame SHALL be emitted on the connection with the admission error

### Requirement: RunTask with preallocated capacity

The `RunTask` service SHALL provide a variant that runs a task under a caller-supplied admission scope instead of calling `AdmissionController::Admit`. The variant SHALL take the parsed `Task`, the originating `Connection`, and the held `AdmissionScopePtr`; it SHALL look up the task handler by `task.type()` and run it with a result sender that owns the supplied scope (releasing it on the final result). The variant SHALL NOT admit, SHALL NOT emit `kTaskRejected`, and SHALL be the execution path for tasks whose capacity was reserved outside the immediate admission call (e.g. the probe scheduler's preallocation). The existing admitting `RunTask` SHALL remain the push path.

#### Scenario: Preallocated task runs without a second admit

- **WHEN** the probe scheduler invokes the scope variant with a held scope and a granted `Task`
- **THEN** admission counters SHALL increase exactly once (by the held scope) and the handler SHALL run with a result sender owning that scope

#### Scenario: Preallocated task releases on final result

- **WHEN** the granted task's handler emits its final result
- **THEN** the sender SHALL release the supplied scope, triggering the capacity-release notification

### Requirement: Probe scheduler registers as capacity observer

The nodeagent `"probe"` local scheduler SHALL register itself as a capacity-release observer with the shared `AdmissionController` (via `RegisterCapacityObserver`) at construction, so that every capacity release — including those caused by cancel/decline of its own reservations — wakes its queue-walking `ProcessCommand`. It SHALL handle the `CAPACITY_RELEASED` command type; other command types SHALL be ignored. This is the first active consumer of the observer mechanism established for local schedulers.

#### Scenario: Probe scheduler wakes on capacity release

- **WHEN** a task completes on the node and the probe scheduler holds queued probes
- **THEN** the scheduler SHALL be woken by `CAPACITY_RELEASED` and SHALL walk its queue, admitting and pulling what now fits

#### Scenario: Spurious wakeup is a no-op

- **WHEN** the probe scheduler receives `CAPACITY_RELEASED` but no queued probe's requirements are now satisfiable
- **THEN** the scheduler SHALL take no admission or forwarding action

### Requirement: Push local scheduler

The system SHALL provide a `"push"` local scheduler registered in `Registry<NodeSchedulerFactory>`. It SHALL implement `RequiredProtocol() == "push"`, handle the `kTaskSubmission` frame type, and for each received `kTaskSubmission` SHALL parse the `Task` and invoke `RunTask`. Its behavior SHALL preserve the pre-change nodeagent behavior exactly: a malformed `Task` is dropped, a task type with no registered handler is dropped, an unadmitted task is rejected with `kTaskRejected`, and an admitted task runs to completion with its results emitted.

#### Scenario: Push scheduler preserves task submission behavior

- **WHEN** a valid `kTaskSubmission` for a registered handler arrives
- **THEN** the scheduler SHALL admit the task and run the handler
- **AND** the resulting `kResult` frame SHALL be emitted on the connection

#### Scenario: Push scheduler drops an unknown frame type

- **WHEN** a frame of a type the push scheduler does not handle arrives
- **THEN** the scheduler SHALL ignore it (drop or log), as before

### Requirement: NodeagentTlvHandler is a frame dispatcher

`NodeagentTlvHandler` SHALL become a thin dispatcher. It SHALL route frames by the TLV frame `type_id` (the `uint8_t` `TlvFrame::type_id` field, e.g. `kTaskSubmission`) to the local scheduler whose `HandledFrameTypes()` contains that type, and SHALL continue to own the core connection responsibilities (sending the `kNodeAdvertisement` first frame) while core-owned frames such as `kNodeState` broadcasting SHALL remain handled by their existing components. The dispatch key SHALL be the TLV frame `type_id`, NOT the `Task.type()` string (which lives in the frame payload and is only consulted later by `RunTask`). Frames handled by no local scheduler and by no core component SHALL be dropped.

#### Scenario: Submission frames route to the local scheduler that handles them

- **WHEN** a `kTaskSubmission` frame arrives and a configured local scheduler lists that type in its `HandledFrameTypes()`
- **THEN** the dispatcher SHALL forward the frame to that scheduler's `HandleFrame`

#### Scenario: Unhandled frame types are dropped

- **WHEN** a frame of an unknown type arrives and no component handles it
- **THEN** the dispatcher SHALL drop the frame

### Requirement: The bundled default scheduler owns the child-task policy

The nodeagent SHALL provide a bundled `default` local scheduler (factory name `"default"`) that owns the node's child-task policy. Its `Schedule(const task::Task&, gateway::ResultReceiverPtr)` SHALL implement *run locally if capacity allows, else forward*: it SHALL first register the child's receiver in its own `LocalResultReceiverStorage` under the child's id, then attempt admission through the shared `AdmissionController`; on success it SHALL run the child via the sender-backed `RunTask` overload with a `StorageResultSender` bound to that storage; on admission failure (or when no local handler claims the child's type) it SHALL forward the child through the node's `ChildTaskForwarder` (`GatewayClient`). When forwarding is unavailable or fails, it SHALL deliver an error through the registered receiver and erase the entry — the parent never hangs. The `default` scheduler SHALL advertise no wire protocol (`RequiredProtocol()` empty) and SHALL own the node's child-outcome frame types (`kResult`, `kTaskRejected`; see `child-result-routing`). Its config message (`DefaultSchedulerConfig`) SHALL be empty.

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
