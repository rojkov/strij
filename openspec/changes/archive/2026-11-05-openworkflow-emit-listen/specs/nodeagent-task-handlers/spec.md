# Spec Delta

## MODIFIED Requirements

### Requirement: TaskHandlerFactory interface
The system SHALL define a `TaskHandlerFactory` interface with `Name()`, `CreateEmptyConfigProto()`, and `Create(const ::google::protobuf::Message& config, const TaskHandlerDeps& deps)` returning a `std::unique_ptr<TaskHandler>`. `Name()` SHALL return the task type string the factory produces handlers for. Task handler factories SHALL be registered in `Registry<TaskHandlerFactory>` via the existing `REGISTER_FACTORY` macros. `TaskHandlerDeps` SHALL be the nodeagent task-handler dependency bundle, exposing only the services task handlers need: the event dispatcher, the shared `FunctionResolver`, the `ChildTaskSubmitter`, and the node `EventBus`. It SHALL NOT expose scheduler-only or fetcher-only services. The `EventBus` reference SHALL be valid whenever a handler factory's `Create` is invoked.

#### Scenario: Factory name matches task type
- **WHEN** `TaskHandlerFactory::Name()` is called on a factory
- **THEN** the returned string SHALL be the task type handled by that factory (e.g. `"echo"`)

#### Scenario: Factory creates a handler from typed config
- **WHEN** `Create(config, deps)` is called with a valid typed config and a `TaskHandlerDeps`
- **THEN** a `std::unique_ptr<TaskHandler>` SHALL be returned

#### Scenario: Handler factories do not see scheduler services
- **WHEN** a task handler factory receives its `TaskHandlerDeps`
- **THEN** the bundle SHALL NOT expose `RunTaskService`, `AdmissionController`, or `DataDependencyFetcherRouter`

#### Scenario: Workflow handler obtains the event bus
- **WHEN** a workflow task handler factory retrieves `deps.event_bus_` during `Create`
- **THEN** the reference SHALL be the node-global event bus shared across handler instances and connections

#### Scenario: Non-event handlers are unaffected
- **WHEN** a task handler that never publishes or listens is created
- **THEN** it SHALL NOT be required to retain or inspect the event bus
