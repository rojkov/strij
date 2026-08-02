# extension-registry

## Purpose

Provide compile-time extension registration infrastructure for Carrot, following the Envoy proxy pattern. Extensions are registered via a static macro, looked up by name from a typed registry, and configured via protobuf `Any`.

## MODIFIED Requirements

### Requirement: FactoryContext

The system SHALL provide an abstract `FactoryContext` interface with `Dispatcher()` and `Logger()` accessors, and a general `FactoryContextImpl` concrete implementation storing a `DispatcherSharedPtr`. The context SHALL be passed to every factory's `Create()` method. A `MockFactoryContext` SHALL be available for unit tests that need to supply a context without a real dispatcher.

#### Scenario: FactoryContext provides dispatcher and logger

- **WHEN** a `FactoryContextImpl` is constructed with a valid `DispatcherSharedPtr`
- **AND** `context.Dispatcher()` is called
- **THEN** a valid `event::Dispatcher&` is returned
- **AND** when `context.Logger()` is called, a valid `logging::Logger&` is returned

#### Scenario: MockFactoryContext supports unit tests

- **WHEN** a test constructs a `MockFactoryContext`
- **THEN** it SHALL satisfy the `FactoryContext` interface with configurable `Dispatcher()` and `Logger()` expectations
