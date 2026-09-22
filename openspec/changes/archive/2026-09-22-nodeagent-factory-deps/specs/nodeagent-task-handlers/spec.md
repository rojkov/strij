# Spec Delta

## MODIFIED Requirements

### Requirement: TaskHandlerFactory interface

The system SHALL define a `TaskHandlerFactory` interface with `Name()`, `CreateEmptyConfigProto()`, and `Create(const ::google::protobuf::Message& config, const TaskHandlerDeps& deps)` returning a `std::unique_ptr<TaskHandler>`. `Name()` SHALL return the task type string the factory produces handlers for. Task handler factories SHALL be registered in `Registry<TaskHandlerFactory>` via the existing `REGISTER_FACTORY` macros. `TaskHandlerDeps` SHALL be the nodeagent task-handler dependency bundle, exposing only the services task handlers need: the event dispatcher, the shared `FunctionResolver`, and the `ChildTaskSubmitter`. It SHALL NOT expose scheduler-only or fetcher-only services.

#### Scenario: Factory name matches task type

- **WHEN** `TaskHandlerFactory::Name()` is called on a factory
- **THEN** the returned string SHALL be the task type handled by that factory (e.g. `"echo"`)

#### Scenario: Factory creates a handler from typed config

- **WHEN** `Create(config, deps)` is called with a valid typed config and a `TaskHandlerDeps`
- **THEN** a `std::unique_ptr<TaskHandler>` SHALL be returned

#### Scenario: Handler factories do not see scheduler services

- **WHEN** a task handler factory receives its `TaskHandlerDeps`
- **THEN** the bundle SHALL NOT expose `RunTaskService`, `AdmissionController`, or `DataDependencyFetcherRouter`

### Requirement: TaskHandlerFactory may access the child-task submitter at construction

`TaskHandlerFactory::Create(config, deps)` SHALL be able to retrieve a `ChildTaskSubmitter` from the `TaskHandlerDeps` bundle in order to pass it to a handler that submits child tasks. Only such handlers SHALL request it; the `TaskHandler` interface (`HandleTask(const task::Task&, ResultSenderPtr)`) and the sender-only constructor shape of existing handlers SHALL remain unchanged. A handler that constructs with the submitter SHALL store it as an immutable constructor dependency for the handler's lifetime. The submitter in the bundle SHALL be a valid, node-global submitter whenever any handler factory's `Create` is invoked: the nodeagent SHALL build the child scheduler router before it builds any task handler.

#### Scenario: Workflow handler factory wires the submitter

- **WHEN** a workflow handler factory's `Create(config, deps)` retrieves `deps.child_task_submitter_`
- **THEN** the returned submitter SHALL be a valid node-global submitter shared across handler instances
- **AND** it SHALL be passed to the handler's constructor and retained by the handler

#### Scenario: Non-submitting handlers never see the submitter

- **WHEN** the `echo` and `piped_executable` factories call `Create`
- **THEN** their handlers SHALL be constructed exactly as before, with no submitter parameter

## REMOVED Requirements

### Requirement: TaskHandlerManager supports runtime reconfiguration

**Reason**: The manager's public mutation surface is minimized. `RemoveHandler` had no production caller (only its own unit test and this speculative seam), and no runtime reconfiguration path is wired.

**Migration**: Register handlers programmatically with `AddHandler` (used by `LoadTaskHandlers` and test setup); runtime removal is no longer supported.

## ADDED Requirements

### Requirement: TaskHandlerManager supports handler registration

The system SHALL expose `AddHandler(type, handler)` on `TaskHandlerManager` as the seam for programmatic handler registration. `LoadTaskHandlers` SHALL use it internally, and it remains public for test setup. In this version the initial set SHALL come only from configuration; no runtime reconfiguration path SHALL be wired.

#### Scenario: Add a handler

- **WHEN** `AddHandler("calc", handler)` is called
- **THEN** `GetHandler("calc")` SHALL return the added handler
