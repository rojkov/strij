# node-local-scheduler

## Purpose

Defines the nodeagent-side local scheduler extension category: how local scheduling extensions are registered, configured, and constructed per `NodeAgentConfig.schedulers` entry, their `Scheduler`/`CommandHandler` roles, the shared core-owned `RunTask` execution path, the frame-dispatcher role of `NodeagentTlvHandler`, and the reserved child-task `Schedule` seam.

## Requirements

### Requirement: Node local scheduler extension interface

The system SHALL define nodeagent-side local scheduler extensions implementing the same `Scheduler` interface as gateway schedulers: `Schedule(const task::Task&, ResultReceiverPtr)` (fire-and-forget), `RequiredProtocol() -> std::string_view`, `HandleFrame(io::TlvFrame, io::Connection&)` (default no-op), and `HandledFrameTypes() -> std::span<const uint8_t>` (default empty). A `NodeSchedulerFactory` SHALL be registered in `Registry<NodeSchedulerFactory>` via the existing `REGISTER_FACTORY` macros, following the `ExtensionConfig` configuration pattern, and SHALL be created with a `NodeagentFactoryContext` exposing Dispatcher, Logger, FunctionResolver, AdmissionController, and a RunTask service. The nodeagent SHALL construct one local scheduler instance per `NodeAgentConfig.schedulers` entry; each instance implements a single scheduling protocol (its `RequiredProtocol()`), so a node can serve gateways running different schedulers by hosting one instance per protocol. All instances SHALL share the node's `AdmissionController` and `RunTask` service; frame dispatch SHALL be by TLV type-id, so instance ownership never overlaps.

#### Scenario: Node scheduler factory is registered and created

- **WHEN** a `NodeSchedulerFactory` with name `"push"` is registered and looked up in `Registry<NodeSchedulerFactory>`
- **THEN** the factory SHALL be found
- **AND** `Create(config, context)` SHALL return a `std::unique_ptr<Scheduler>`

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

### Requirement: RunTask service

The system SHALL provide a core-owned `RunTask` service accessible to nodeagent schedulers through `NodeagentFactoryContext`. `RunTask` SHALL take a parsed `Task` and the originating `Connection`, look up the task handler by `task.type()`, admit the task via the `AdmissionController`, and on admission run the handler with a result sender that emits `kResult` frames (and releases the admission scope on the final result). When admission fails, `RunTask` SHALL emit a `kTaskRejected` frame with the admission error. The execution path SHALL be the single shared path used by every local scheduler.

#### Scenario: RunTask executes an admitted task

- **WHEN** `RunTask` is invoked with a task whose type has a registered handler and whose admission succeeds
- **THEN** the task handler SHALL be invoked with a result sender
- **AND** the handler's results SHALL be emitted as `kResult` frames on the connection

#### Scenario: RunTask rejects an unadmissible task

- **WHEN** `RunTask` is invoked with a task whose admission fails (e.g. a pool is exhausted)
- **THEN** a `kTaskRejected` frame SHALL be emitted on the connection with the admission error

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

### Requirement: Child-task Schedule is reserved

Nodeagent local schedulers SHALL implement the `Schedule` facet of the `Scheduler` interface for locally-originated tasks (e.g. child tasks spawned by a workflow handler). The Phase 0 implementation SHALL deliver an error to the receiver when `Schedule` is invoked, because local execution and gateway forwarding of child tasks is not yet implemented. The seam SHALL be present so the future child-task path lands without interface changes.

#### Scenario: Schedule invocation is rejected in Phase 0

- **WHEN** `Schedule(task, receiver)` is invoked on the nodeagent local scheduler
- **THEN** the scheduler SHALL deliver an error outcome to the receiver
