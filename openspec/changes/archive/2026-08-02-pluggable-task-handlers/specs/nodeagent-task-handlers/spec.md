# nodeagent-task-handlers

## Purpose

Defines the pluggable task handler extension category for the nodeagent: the `TaskHandler` interface, the `TaskHandlerFactory` factory interface, the `TaskHandlerManager` that owns and routes to instantiated handlers, the `ResultSender` result delivery abstraction, and the first extension, `EchoTaskHandler`.

## ADDED Requirements

### Requirement: TaskHandler interface
The system SHALL define an abstract `TaskHandler` interface in the `strij::extensions` namespace with a single pure virtual `HandleTask(const strij::task::Task& task, ResultSender& sender)`. The handler SHALL process the task and deliver zero or more `TaskResult` messages to the originating connection through the provided `ResultSender`. A synchronous handler SHALL deliver its result before `HandleTask` returns.

#### Scenario: Echo handler delivers a result synchronously
- **WHEN** `HandleTask(task, sender)` is called on a task handler
- **THEN** the handler SHALL process the `task`
- **AND** deliver a `TaskResult` via `sender.Send(result)`

### Requirement: TaskHandlerFactory interface
The system SHALL define a `TaskHandlerFactory` interface with `Name()`, `CreateEmptyConfigProto()`, and `Create(const ::google::protobuf::Message& config, FactoryContext& context)` returning a `std::unique_ptr<TaskHandler>`. `Name()` SHALL return the task type string the factory produces handlers for. Task handler factories SHALL be registered in `Registry<TaskHandlerFactory>` via the existing `REGISTER_FACTORY` macros.

#### Scenario: Factory name matches task type
- **WHEN** `TaskHandlerFactory::Name()` is called on a factory
- **THEN** the returned string SHALL be the task type handled by that factory (e.g. `"echo"`)

#### Scenario: Factory creates a handler from typed config
- **WHEN** `Create(config, context)` is called with a valid typed config
- **THEN** a `std::unique_ptr<TaskHandler>` SHALL be returned

### Requirement: TaskHandlerManager routes tasks by type
The system SHALL provide a `TaskHandlerManager` that owns instantiated `TaskHandler`s keyed by task type (factory `Name()`), with `GetHandler(const std::string& type)` returning a `TaskHandler*` or `nullptr`. The manager SHALL be constructed once at startup from the configured task handlers and SHALL be shared across all nodeagent connections via `shared_ptr`.

#### Scenario: Lookup returns the registered handler
- **WHEN** a manager contains a handler for type `"echo"` and `GetHandler("echo")` is called
- **THEN** a pointer to the echo task handler SHALL be returned

#### Scenario: Lookup of an unregistered type
- **WHEN** `GetHandler("unknown")` is called on a manager with no handler for `"unknown"`
- **THEN** `nullptr` SHALL be returned

### Requirement: TaskHandlerManager supports runtime reconfiguration
The system SHALL expose mutation operations on `TaskHandlerManager` (`AddHandler(type, handler)` and `RemoveHandler(type)`) as the seam for future runtime reconfiguration of the enabled task handler set. In this version the initial set SHALL come only from configuration; no runtime reconfiguration path SHALL be wired.

#### Scenario: Add a handler at runtime
- **WHEN** `AddHandler("calc", handler)` is called
- **THEN** `GetHandler("calc")` SHALL return the added handler

#### Scenario: Remove a handler at runtime
- **WHEN** `RemoveHandler("echo")` is called on a manager containing the echo handler
- **THEN** `GetHandler("echo")` SHALL return `nullptr`

### Requirement: EchoTaskHandler extension
The system SHALL provide an `EchoTaskHandler` extension registered as `"echo"` in `Registry<TaskHandlerFactory>`. `HandleTask` SHALL build a `TaskResult` with `id = task.id()`, `body = task.body()`, `is_final = true`, and deliver it via the `ResultSender`.

#### Scenario: Echo handler reproduces the task
- **WHEN** `HandleTask` is invoked with a `Task` with id and body
- **THEN** the handler SHALL deliver a `TaskResult` with the same id, the same body, and `is_final = true`

### Requirement: Task handler configuration loading semantics
The nodeagent SHALL load the enabled task handler list from `NodeAgentConfig.task_handlers`. For each entry, the nodeagent SHALL resolve the factory by `name` in `Registry<TaskHandlerFactory>`, unpack the `Any` typed_config into the factory's empty config proto, and call `Create`. If the config list is empty, the nodeagent SHALL log a warning and continue (the manager may be reconfigured at runtime in the future). If an entry names an unregistered factory, the nodeagent SHALL fail to start with an error.

#### Scenario: Empty task_handlers list warns
- **WHEN** the nodeagent starts with an empty `task_handlers` list
- **THEN** a warning SHALL be logged
- **AND** the nodeagent SHALL continue starting with a manager that routes no task types

#### Scenario: Unknown handler name fails startup
- **WHEN** the nodeagent starts with a `task_handlers` entry whose `name` is not registered in `Registry<TaskHandlerFactory>`
- **THEN** startup SHALL fail with an error naming the unknown handler

#### Scenario: Handler instance built from typed config
- **WHEN** the nodeagent starts with a `task_handlers` entry `name="echo"` and a matching typed_config
- **THEN** the manager SHALL contain an `EchoTaskHandler` for type `"echo"`
