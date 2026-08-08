# function-resolver

## Purpose

Defines how task handlers that run executables resolve the `function` parameter into an executable path, keeping the resolution behind an interface so filesystem policy can be added later without changing handlers.

## Requirements

### Requirement: FunctionResolver interface
The system SHALL define a `FunctionResolver` interface in the extension layer with a `Resolve(std::string_view reference)` operation returning an executable path or an error. Task handlers that consume the `function` parameter SHALL obtain the executable path through this interface, never by touching the filesystem directly.

#### Scenario: Resolve returns a path
- **WHEN** `Resolve("my-tool")` succeeds
- **THEN** a path to the executable SHALL be returned

#### Scenario: Resolve of an empty reference fails
- **WHEN** `Resolve("")` is called
- **THEN** an error SHALL be returned

### Requirement: LocalFunctionResolver
The system SHALL provide a `LocalFunctionResolver` implementing `FunctionResolver`. It SHALL return the reference unchanged as the executable path. Filesystem validation and executable allowlisting SHALL be deferred until the deployment extension.

#### Scenario: LocalFunctionResolver returns the reference as the path
- **WHEN** `LocalFunctionResolver::Resolve("/usr/bin/cat")` is called
- **THEN** "/usr/bin/cat" SHALL be returned

#### Scenario: LocalFunctionResolver rejects an empty reference
- **WHEN** `LocalFunctionResolver::Resolve("")` is called
- **THEN** an error SHALL be returned

### Requirement: FactoryContext exposes FunctionResolver
`FactoryContext` SHALL expose a `FunctionResolver()` accessor so task handler factories can obtain the shared resolver at construction. The resolver SHALL be built once at nodeagent startup and shared across all task handlers.

#### Scenario: Handler factory obtains the shared resolver
- **WHEN** a task handler factory calls `context.FunctionResolver()`
- **THEN** a reference to the shared resolver SHALL be returned

#### Scenario: Resolver is shared across handlers
- **WHEN** two task handler factories obtain the resolver from the same `FactoryContext`
- **THEN** both SHALL receive the same resolver instance
