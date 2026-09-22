# Spec Delta

## RENAMED Requirements

- FROM: `### Requirement: FactoryContext exposes FunctionResolver`
- TO: `### Requirement: TaskHandlerDeps exposes FunctionResolver`

## MODIFIED Requirements

### Requirement: TaskHandlerDeps exposes FunctionResolver

`TaskHandlerDeps` SHALL expose a function-resolver reference so task handler factories can obtain the shared resolver at construction. The resolver SHALL be built once at nodeagent startup and shared across all task handlers.

#### Scenario: Handler factory obtains the shared resolver

- **WHEN** a task handler factory reads the resolver from its `TaskHandlerDeps`
- **THEN** a reference to the shared resolver SHALL be returned

#### Scenario: Resolver is shared across handlers

- **WHEN** two task handler factories obtain the resolver from their `TaskHandlerDeps`
- **THEN** both SHALL receive the same resolver instance
