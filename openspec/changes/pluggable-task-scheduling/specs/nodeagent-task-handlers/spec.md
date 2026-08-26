## MODIFIED Requirements

### Requirement: TaskHandlerFactory interface

The system SHALL define a `TaskHandlerFactory` interface with `Name()`, `CreateEmptyConfigProto()`, and `Create(const ::google::protobuf::Message& config, NodeagentFactoryContext& context)` returning a `std::unique_ptr<TaskHandler>`. `Name()` SHALL return the task type string the factory produces handlers for. Task handler factories SHALL be registered in `Registry<TaskHandlerFactory>` via the existing `REGISTER_FACTORY` macros. `NodeagentFactoryContext` SHALL be the nodeagent-side factory context (Dispatcher, Logger, FunctionResolver, AdmissionController, RunTask service); it SHALL NOT expose gateway-only services.

#### Scenario: Factory name matches task type

- **WHEN** `TaskHandlerFactory::Name()` is called on a factory
- **THEN** the returned string SHALL be the task type handled by that factory (e.g. `"echo"`)

#### Scenario: Factory creates a handler from typed config

- **WHEN** `Create(config, context)` is called with a valid typed config and a `NodeagentFactoryContext`
- **THEN** a `std::unique_ptr<TaskHandler>` SHALL be returned
