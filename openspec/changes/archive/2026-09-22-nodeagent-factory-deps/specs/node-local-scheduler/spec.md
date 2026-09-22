# Spec Delta

## MODIFIED Requirements

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
