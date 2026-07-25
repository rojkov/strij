# extension-registry

## Purpose

Provide compile-time extension registration infrastructure for Carrot, following the Envoy proxy pattern. Extensions are registered via a static macro, looked up by name from a typed registry, and configured via protobuf `Any`.

## Requirements

### R1: Registry Template
- `Registry<FactoryInterface>` is a singleton per factory type
- `registerFactory(name, factory)` stores a raw pointer (lifetime managed by static registration)
- `getFactory(name)` returns `FactoryInterface*` or `nullptr`
- `getRegisteredNames()` returns all registered names

### R2: Registration Macro
- `REGISTER_FACTORY(FactoryClass, FactoryInterface)` performs static initialization
- The macro creates an anonymous-namespace struct whose constructor calls `Registry::registerFactory`
- Registration happens before `main()` — no explicit init call needed
- The factory object is heap-allocated via `new` and lives for the process lifetime

### R3: FactoryContext
- Abstract `FactoryContext` interface with `dispatcher()` and `logger()` accessors
- `GatewayFactoryContext` concrete implementation stores a `DispatcherSharedPtr`
- Passed to every factory's `create()` method

### R4: ExtensionConfig Protobuf
- `ExtensionConfig` message with `name` (string) and `typed_config` (`google.protobuf.Any`)
- Used as the config type for each extension category field in `GatewayConfig`

### R5: Bazel Integration
- Header-only `extension_registry_lib` target (no runtime deps beyond standard library)
- `factory_context_lib` target depends on `dispatcher_interface` and `log_lib`
- Users add extension `.cc` files to `gateway/BUILD.bazel` deps to link them in

## Scenarios

### S1: Factory registration via macro

**Given** a factory class `MyFactory` implementing `SomeFactoryInterface` with `name()` returning `"my_factory"`
**And** a `REGISTER_FACTORY(MyFactory, SomeFactoryInterface)` invocation in a `.cc` file
**When** the program starts (before `main()`)
**Then** `Registry<SomeFactoryInterface>::instance().getFactory("my_factory")` returns a non-null pointer
**And** the returned pointer's `name()` equals `"my_factory"`

### S2: Multiple factories of the same type

**Given** two factory classes `AlphaFactory` and `BetaFactory` both implementing `SomeFactoryInterface`
**And** both registered via `REGISTER_FACTORY`
**When** querying `Registry<SomeFactoryInterface>::instance().getRegisteredNames()`
**Then** the result contains both `"alpha"` and `"beta"`

### S3: Unregistered factory lookup

**Given** a `Registry<SomeFactoryInterface>` with no factory named `"nonexistent"`
**When** calling `getFactory("nonexistent")`
**Then** the result is `nullptr`

### S4: FactoryContext provides Dispatcher and Logger

**Given** a `GatewayFactoryContext` constructed with a valid `DispatcherSharedPtr`
**When** calling `context.dispatcher()`
**Then** a valid `event::Dispatcher&` is returned
**When** calling `context.logger()`
**Then** a valid `logging::Logger&` is returned

### S5: ExtensionConfig protobuf round-trip

**Given** an `ExtensionConfig` with `name = "etcd"` and a `typed_config` containing an `EtcdConfig` message
**When** serializing to bytes and deserializing back
**Then** `name` equals `"etcd"`
**And** `typed_config` unpacks to the same `EtcdConfig` values

### S6: Bazel targets compile

**Given** the `extension_registry_lib` and `factory_context_lib` Bazel targets
**When** building `//src/core/extensions:all`
**Then** the build succeeds with no errors