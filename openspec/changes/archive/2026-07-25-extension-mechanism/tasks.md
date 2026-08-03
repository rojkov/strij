# Tasks

## 1. Extension Registry Infrastructure

- [x] 1.1 Create `src/core/extensions/extension_registry.hh`
  - Header-only `Registry<FactoryInterface>` template
  - `REGISTER_FACTORY` and `REGISTER_FACTORY_FULLY_QUALIFIED` macros
  - Namespace: `strij::extensions`
- [x] 1.2 Create `src/core/extensions/factory_context.hh`
  - `FactoryContext` abstract class with `Dispatcher()` and `Logger()` pure virtuals
  - `GatewayFactoryContext` concrete class storing `DispatcherSharedPtr`
- [x] 1.3 Create `src/core/extensions/factory_context.cc`
  - `GatewayFactoryContext` method implementations
- [x] 1.4 Create `src/core/extensions/BUILD.bazel`
  - `extension_registry_lib` target (header-only)
  - `factory_context_lib` target (deps: `dispatcher_interface`, `log_lib`)

## 2. ExtensionConfig Protobuf

- [x] 2.1 Create `src/core/config/proto/extensions.proto`
  - `ExtensionConfig` message: `string name = 1`, `google.protobuf.Any typed_config = 2`
- [x] 2.2 Update `src/core/config/proto/BUILD.bazel`
  - Added `extensions.proto` to config_proto srcs
  - Added `@protobuf//:any_proto` to deps

## 3. GatewayConfig Proto Changes

- [x] 3.1 Update `src/core/config/proto/gateway.proto`
  - Import `extensions.proto`
  - Add `ExtensionConfig node_discovery = 4` to `GatewayConfig`

## 4. NodeDiscovery Interface

- [x] 4.1 Create `src/extensions/node_discovery/node_discovery.hh`
  - `NodeInfo` struct with `std::string address`
  - `NodeDiscovery` abstract class: `Start(DiscoveryCallback)`, `Stop()`
  - `NodeDiscoveryFactory` abstract class: `Name()`, `CreateEmptyConfigProto()`, `Create(config, context)`
- [x] 4.2 Create `src/extensions/node_discovery/BUILD.bazel`
  - `node_discovery_interface` header-only target

## 5. StaticNodeDiscovery (built-in)

- [x] 5.1 Create `src/extensions/node_discovery/static/static_node_discovery.proto`
  - `StaticNodeDiscoveryConfig`: `repeated string addresses = 1`
- [x] 5.2 Create `src/extensions/node_discovery/static/static_node_discovery.hh`
  - `StaticNodeDiscovery` implementing `NodeDiscovery`
  - `StaticNodeDiscoveryFactory` implementing `NodeDiscoveryFactory`
- [x] 5.3 Create `src/extensions/node_discovery/static/static_node_discovery.cc`
  - `Start()`: invoke callback immediately with addresses
  - `Stop()`: no-op
  - `Create()`: unpack config, validate non-empty, construct
  - `REGISTER_FACTORY_FULLY_QUALIFIED` for auto-registration
- [x] 5.4 Create `src/extensions/node_discovery/static/BUILD.bazel`
  - `static_node_discovery_proto` proto_library
  - `static_node_discovery_cc_proto` cc_proto_library
  - `static_node_discovery_lib` strij_cc_library

## 6. Gateway Wiring

- [x] 6.1 Update `src/exe/gateway/gateway.cc`
  - Include extension headers
  - Look up `config.node_discovery()` in registry
  - Unpack `typed_config`, call `factory->Create()`
  - Fallback to `node_connections[]` if no extension configured
  - Use `node_discovery->Start()` to get addresses, then create NodeDirectory
- [x] 6.2 Update `src/exe/gateway/BUILD.bazel`
  - Add deps: `extension_registry_lib`, `factory_context_lib`
  - Add deps: `node_discovery_interface`, `static_node_discovery_lib`
  - Add dep: `@protobuf//:protobuf`

## 7. Verification

- [x] 7.1 `make build` passes
- [x] 7.2 `make test` passes (4/4 tests pass)
- [x] 7.3 Existing gateway behavior unchanged with legacy config
- [x] 7.4 Add `node_discovery` to a test config, verify gateway discovers nodes via extension
