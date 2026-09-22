# extension-registry

## Purpose

Provide compile-time extension registration infrastructure for Strij, following the Envoy proxy pattern. Extensions are registered via a static macro, looked up by name from a typed registry, and configured via protobuf `Any`.

## Requirements

### Requirement: Registry template

The system SHALL provide a `Registry<FactoryInterface>` that is a singleton per factory type. It SHALL support `registerFactory(name, factory)` storing a raw pointer (lifetime managed by static registration), `getFactory(name)` returning `FactoryInterface*` or `nullptr`, and `getRegisteredNames()` returning all registered names.

#### Scenario: Multiple factories of the same type

- **WHEN** two factory classes `AlphaFactory` and `BetaFactory` both implementing `SomeFactoryInterface` are registered via `REGISTER_FACTORY`
- **AND** `Registry<SomeFactoryInterface>::instance().getRegisteredNames()` is queried
- **THEN** the result contains both `"alpha"` and `"beta"`

#### Scenario: Unregistered factory lookup

- **WHEN** `getFactory("nonexistent")` is called on a `Registry<SomeFactoryInterface>` with no factory named `"nonexistent"`
- **THEN** the result is `nullptr`

### Requirement: Registration macro

The system SHALL provide a `REGISTER_FACTORY(FactoryClass, FactoryInterface)` macro that performs static initialization by creating an anonymous-namespace struct whose constructor calls `Registry::registerFactory`. Registration SHALL happen before `main()` with no explicit init call, and the factory object SHALL be heap-allocated via `new` and live for the process lifetime.

#### Scenario: Factory registration via macro

- **WHEN** a factory class `MyFactory` implementing `SomeFactoryInterface` with `name()` returning `"my_factory"` is registered via `REGISTER_FACTORY(MyFactory, SomeFactoryInterface)` in a `.cc` file
- **AND** the program starts (before `main()`)
- **THEN** `Registry<SomeFactoryInterface>::instance().getFactory("my_factory")` returns a non-null pointer
- **AND** the returned pointer's `name()` equals `"my_factory"`

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

### Requirement: ExtensionConfig protobuf

The system SHALL define an `ExtensionConfig` message with `name` (string) and `typed_config` (`google.protobuf.Any`), used as the config type for each extension category field in `GatewayConfig`.

#### Scenario: ExtensionConfig protobuf round-trip

- **WHEN** an `ExtensionConfig` with `name = "etcd"` and a `typed_config` containing an `EtcdConfig` message is serialized to bytes and deserialized back
- **THEN** `name` equals `"etcd"`
- **AND** `typed_config` unpacks to the same `EtcdConfig` values

### Requirement: Bazel integration

The system SHALL provide a header-only `extension_registry_lib` Bazel target (no runtime deps beyond standard library) and a `factory_context_interface` target depending on `dispatcher_interface`. Interface targets SHALL be named with the `_interface` suffix. Users SHALL add extension `.cc` files to `gateway/BUILD.bazel` deps to link them in.

#### Scenario: Bazel targets compile

- **WHEN** building `//src/core/extensions:all`
- **THEN** the build succeeds with no errors
