# Spec Delta

## MODIFIED Requirements

### Requirement: PipedExecutableTaskHandlerFactory

The system SHALL provide a `PipedExecutableTaskHandlerFactory` registered as `"piped_executable"` with a config proto `PipedExecutableTaskHandlerConfig`. The factory SHALL obtain the shared `FunctionResolver` from its `TaskHandlerDeps` bundle and pass it to the handler.

#### Scenario: Factory creates a handler with the shared resolver

- **WHEN** `Create(config, deps)` is called
- **THEN** a `PipedExecutableTaskHandler` using the resolver from `deps` SHALL be returned

#### Scenario: Factory registers under the piped_executable name

- **WHEN** `Name()` is called on the factory
- **THEN** "piped_executable" SHALL be returned
