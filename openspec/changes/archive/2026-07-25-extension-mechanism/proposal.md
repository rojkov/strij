## Why

Strij's gateway currently reads a static list of node addresses from `node_connections[]` in the config file. This hardcodes node discovery — there is no way to add dynamic sources (etcd, Consul, mDNS, DNS SRV) without forking the code. Users also cannot plug in custom discovery logic. The project needs an Envoy-style extension registry: a compile-time factory pattern with protobuf `Any` config, where users add their own `.cc` files and link them via Bazel.

## What Changes

- Add core extension registry infrastructure: `Registry<Factory>` template, `REGISTER_FACTORY` macro, `FactoryContext` (Dispatcher + Logger)
- Add `NodeDiscovery` extension category with abstract interface and factory
- Add built-in `StaticNodeDiscovery` implementation (reads from legacy `node_connections[]` config)
- Add `node_discovery` field to `GatewayConfig` protobuf (typed `ExtensionConfig`)
- Refactor `gateway.cc` to instantiate NodeDiscovery from the registry instead of hardcoding `NodeDirectory`
- Backward-compatible: configs without `node_discovery` fall back to `node_connections[]`

## Capabilities

### New Capabilities
- `extension-registry`: Core registry infrastructure — `Registry<T>` template, `REGISTER_FACTORY` macro, `FactoryContext`, `ExtensionConfig` protobuf
- `node-discovery`: Pluggable node discovery with abstract `NodeDiscovery` interface, `NodeDiscoveryFactory`, and built-in `StaticNodeDiscovery`

### Modified Capabilities
- `gateway-config`: `GatewayConfig` protobuf gains `node_discovery` field of type `ExtensionConfig`
- `gateway-core`: `gateway.cc` startup wiring uses `Registry<NodeDiscoveryFactory>` to create the discovery instance

## Impact

**New files:**
- `src/core/extensions/extension_registry.hh` — Registry template + REGISTER_FACTORY macro
- `src/core/extensions/factory_context.hh` — FactoryContext interface + GatewayFactoryContext
- `src/core/extensions/BUILD.bazel`
- `src/core/config/proto/extensions.proto` — ExtensionConfig message
- `src/extensions/node_discovery/node_discovery.hh` — NodeDiscovery + NodeDiscoveryFactory interfaces
- `src/extensions/node_discovery/BUILD.bazel`
- `src/extensions/node_discovery/static/static_node_discovery.hh/.cc` — built-in implementation
- `src/extensions/node_discovery/static/static_node_discovery.proto` — StaticNodeDiscoveryConfig
- `src/extensions/node_discovery/static/BUILD.bazel`

**Modified files:**
- `src/core/config/proto/gateway.proto` — add `node_discovery` field
- `src/core/config/proto/BUILD.bazel` — add extensions_proto, node_discovery_proto
- `src/exe/gateway/gateway.cc` — wire NodeDiscovery from registry
- `src/exe/gateway/BUILD.bazel` — add extension deps

**Dependencies:** Protobuf `google.protobuf.Any`, existing `Dispatcher` and `Logger`

**No breaking changes:** Existing configs without `node_discovery` continue to work via fallback to `node_connections[]`.