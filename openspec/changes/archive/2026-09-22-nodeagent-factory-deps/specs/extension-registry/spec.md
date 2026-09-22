# Spec Delta

## REMOVED Requirements

### Requirement: FactoryContext

**Reason**: The nodeagent no longer passes a shared factory context to its factories; the requirement is superseded by the gateway-scoped `Gateway factory context` requirement plus the nodeagent per-category dependency bundles (`TaskHandlerDeps`, `NodeSchedulerDeps`, `DataDependencyFetcherDeps`).

**Migration**: Gateway scheduler and node-discovery factories continue to receive `GatewayFactoryContext` (extending the `FactoryContext` base). Nodeagent task-handler, scheduler, and data-dependency-fetcher factories receive their category's dependency bundle instead.

## ADDED Requirements

### Requirement: Gateway factory context

The system SHALL provide an abstract `FactoryContext` base interface with `Dispatcher()` and `SharedDispatcher()` accessors. The gateway side SHALL define `GatewayFactoryContext` extending it (adding `NodeDirectory()` and `ResultReceiverStorage()`) and SHALL pass it to every gateway factory's `Create()` method. The nodeagent side SHALL NOT pass a single shared factory context to its factories: each nodeagent extension category SHALL receive its own dependency bundle (see `nodeagent-task-handlers`, `node-local-scheduler`, `data-dependency-fetcher`). A mock gateway factory context SHALL be available for unit tests that need to supply a context without a real dispatcher.

#### Scenario: Gateway factory context provides the dispatcher

- **WHEN** a gateway factory context is constructed with a valid `DispatcherSharedPtr`
- **AND** `context.Dispatcher()` is called
- **THEN** a valid `event::Dispatcher&` is returned

#### Scenario: Mock gateway factory context supports unit tests

- **WHEN** a test constructs a mock gateway factory context
- **THEN** it SHALL satisfy the `GatewayFactoryContext` interface with configurable `Dispatcher()` and `NodeDirectory()` expectations

#### Scenario: Gateway factories receive the gateway context

- **WHEN** a gateway scheduler or node-discovery factory's `Create()` is called
- **THEN** it SHALL receive a `GatewayFactoryContext&`

#### Scenario: Nodeagent factories receive their category bundle

- **WHEN** a nodeagent task-handler, scheduler, or data-dependency-fetcher factory's `Create()` is called
- **THEN** it SHALL receive its category's dependency bundle and SHALL NOT receive a shared `FactoryContext`

## MODIFIED Requirements

### Requirement: Bazel integration

The system SHALL provide a header-only `extension_registry_lib` Bazel target (no runtime deps beyond standard library) and a `factory_context_interface` target depending on `dispatcher_interface`. Interface targets SHALL be named with the `_interface` suffix. Users SHALL add extension `.cc` files to `gateway/BUILD.bazel` deps to link them in.

#### Scenario: Bazel targets compile

- **WHEN** building `//src/core/extensions:all`
- **THEN** the build succeeds with no errors
