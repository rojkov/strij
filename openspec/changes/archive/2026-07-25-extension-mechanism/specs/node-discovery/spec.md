# node-discovery

## Purpose

Allow the gateway to discover node addresses from pluggable sources (static config, etcd, Consul, mDNS, DNS SRV) instead of hardcoding `node_connections[]`. Users can add new discovery sources by implementing a factory and registering it via `REGISTER_FACTORY`.

## Requirements

### R1: NodeDiscovery Interface
- `NodeDiscovery` has `start(DiscoveryCallback)` and `stop()` methods
- `DiscoveryCallback` is `std::function<void(std::vector<NodeInfo>)>`
- `NodeInfo` contains at minimum an `address` string

### R2: NodeDiscoveryFactory Interface
- `name()` returns the extension name (e.g. `"static"`, `"etcd"`)
- `create(config, context)` takes an unpacked protobuf config message and a `FactoryContext&`
- Returns `std::unique_ptr<NodeDiscovery>`

### R3: StaticNodeDiscovery (built-in)
- Registered as `"static"`
- Config: `StaticNodeDiscoveryConfig` with `repeated string addresses`
- On `start()`, immediately invokes callback with configured addresses
- `stop()` is a no-op

### R4: GatewayConfig Integration
- `GatewayConfig.node_discovery` field of type `ExtensionConfig`
- Gateway looks up `node_discovery.name()` in `Registry<NodeDiscoveryFactory>`
- Unpacks `typed_config` into the factory's expected config type
- Calls `factory->create(unpacked_config, context)`

### R5: Backward Compatibility
- If `node_discovery` is not set in config, gateway falls back to `node_connections[]`
- Legacy `node_connections[]` creates a `StaticNodeDiscovery` internally
- Existing configs work without modification

### R6: Bazel Extension Pattern
- `node_discovery_interface` is a header-only target
- Each implementation (static, etcd, etc.) is a separate `cc_library` + `proto_library`
- User adds their extension to `gateway/BUILD.bazel` deps
- `REGISTER_FACTORY` in the user's `.cc` auto-registers at startup

## Scenarios

### S1: Static discovery from config

**Given** a `GatewayConfig` with `node_discovery` set to `{ name: "static", typed_config: { addresses: ["10.0.0.1:9090", "10.0.0.2:9090"] } }`
**When** gateway starts and looks up `"static"` in `Registry<NodeDiscoveryFactory>`
**Then** the factory creates a `NodeDiscovery` instance
**When** `start(callback)` is called
**Then** the callback is invoked immediately with a vector containing `"10.0.0.1:9090"` and `"10.0.0.2:9090"`

### S2: Static discovery with empty addresses rejected

**Given** a `StaticNodeDiscoveryConfig` with empty `addresses`
**When** `StaticNodeDiscoveryFactory::create()` is called
**Then** an exception is thrown indicating addresses must not be empty

### S3: Fallback to legacy node_connections

**Given** a `GatewayConfig` with no `node_discovery` field set
**And** `node_connections` containing `[{ address: "10.0.0.1:9090" }, { address: "10.0.0.2:9090" }]`
**When** gateway starts
**Then** a `StaticNodeDiscovery` is created internally with those addresses
**And** `start()` delivers those addresses via callback

### S4: Config with both node_discovery and node_connections

**Given** a `GatewayConfig` with `node_discovery` set to `{ name: "static", typed_config: { ... } }`
**And** `node_connections` also populated
**When** gateway starts
**Then** the `node_discovery` extension is used
**And** `node_connections` is ignored

### S5: Unregistered discovery extension

**Given** a `GatewayConfig` with `node_discovery` set to `{ name: "nonexistent", typed_config: { ... } }`
**When** gateway starts and looks up `"nonexistent"` in `Registry<NodeDiscoveryFactory>`
**Then** the factory is not found
**And** gateway falls back to `node_connections[]` if available

### S6: NodeDiscovery stop

**Given** a started `StaticNodeDiscovery` with addresses configured
**When** `stop()` is called
**Then** no error occurs (no-op)

### S7: User adds custom discovery via Bazel

**Given** a user creates `src/extensions/node_discovery/mdns/` with `MdnsNodeDiscovery` and `MdnsNodeDiscoveryFactory`
**And** a `REGISTER_FACTORY(MdnsNodeDiscoveryFactory, NodeDiscoveryFactory)` in the `.cc` file
**And** the target added to `gateway/BUILD.bazel` deps
**When** gateway starts with `node_discovery: { name: "mdns", ... }`
**Then** the `MdnsNodeDiscovery` is instantiated and used

### S8: Bazel targets compile

**Given** the `node_discovery_interface` and `static_node_discovery_lib` Bazel targets
**When** building `//src/extensions/node_discovery/...`
**Then** the build succeeds with no errors