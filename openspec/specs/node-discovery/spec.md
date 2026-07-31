# node-discovery

## Purpose

Allow the gateway to discover node addresses from pluggable sources (static config, etcd, Consul, mDNS, DNS SRV) instead of hardcoding `node_connections[]`. Users can add new discovery sources by implementing a factory and registering it via `REGISTER_FACTORY`.

## Requirements

### Requirement: NodeDiscovery interface

The system SHALL provide a `NodeDiscovery` interface with `start(DiscoveryCallback)` and `stop()` methods. `DiscoveryCallback` SHALL be `std::function<void(std::vector<NodeInfo>)>` and `NodeInfo` SHALL contain at minimum an `address` string.

#### Scenario: NodeDiscovery stop

- **WHEN** `stop()` is called on a started `StaticNodeDiscovery` with addresses configured
- **THEN** no error occurs (no-op)

### Requirement: NodeDiscoveryFactory interface

The system SHALL provide a `NodeDiscoveryFactory` interface whose `name()` returns the extension name (e.g. `"static"`, `"etcd"`), and whose `create(config, context)` takes an unpacked protobuf config message and a `FactoryContext&` and returns `std::unique_ptr<NodeDiscovery>`.

#### Scenario: Factory creates a discovery instance

- **WHEN** `NodeDiscoveryFactory::create()` is called with an unpacked config message and a `FactoryContext&`
- **THEN** a `std::unique_ptr<NodeDiscovery>` is returned

### Requirement: StaticNodeDiscovery built-in

The system SHALL provide a built-in `StaticNodeDiscovery` registered as `"static"` with a `StaticNodeDiscoveryConfig` containing `repeated string addresses`. On `start()` it SHALL immediately invoke the callback with the configured addresses, and `stop()` SHALL be a no-op.

#### Scenario: Static discovery from config

- **WHEN** a `GatewayConfig` has `node_discovery` set to `{ name: "static", typed_config: { addresses: ["10.0.0.1:9090", "10.0.0.2:9090"] } }`
- **AND** the gateway looks up `"static"` in `Registry<NodeDiscoveryFactory>`
- **THEN** the factory creates a `NodeDiscovery` instance
- **AND** when `start(callback)` is called, the callback is invoked immediately with a vector containing `"10.0.0.1:9090"` and `"10.0.0.2:9090"`

#### Scenario: Static discovery with empty addresses rejected

- **WHEN** `StaticNodeDiscoveryFactory::create()` is called with a `StaticNodeDiscoveryConfig` with empty `addresses`
- **THEN** an exception is thrown indicating addresses must not be empty

### Requirement: GatewayConfig integration

The system SHALL expose a `GatewayConfig.node_discovery` field of type `ExtensionConfig` that is **required**. The gateway SHALL look up `node_discovery.name()` in `Registry<NodeDiscoveryFactory>`, unpack `typed_config` into the factory's expected config type, and call `factory->create(unpacked_config, context)`.

#### Scenario: User adds custom discovery via Bazel

- **WHEN** a user creates `src/extensions/node_discovery/mdns/` with `MdnsNodeDiscovery` and `MdnsNodeDiscoveryFactory`
- **AND** adds a `REGISTER_FACTORY(MdnsNodeDiscoveryFactory, NodeDiscoveryFactory)` in the `.cc` file
- **AND** adds the target to `gateway/BUILD.bazel` deps
- **AND** the gateway starts with `node_discovery: { name: "mdns", ... }`
- **THEN** the `MdnsNodeDiscovery` is instantiated and used

### Requirement: Error on missing node_discovery

The system SHALL exit with `return 1` and a descriptive error if `node_discovery` is not set in config, including under `--validate-only`. `node_connections[]` SHALL NOT be used as a fallback.

#### Scenario: Node discovery missing from config

- **WHEN** a `GatewayConfig` has no `node_discovery` field set and the gateway starts
- **THEN** the gateway logs an error: "node_discovery extension is required"
- **AND** exits with status 1

#### Scenario: Validate-only catches missing node_discovery

- **WHEN** a `GatewayConfig` has no `node_discovery` field set and the gateway starts with `--validate-only`
- **THEN** the gateway logs an error: "node_discovery extension is required"
- **AND** exits with status 1

### Requirement: Error on unregistered extension name

The system SHALL exit with `return 1` if `node_discovery` is set but the factory name is not found in the registry. The error message SHALL include the missing name and suggest checking the linked libraries.

#### Scenario: Unregistered discovery extension causes error

- **WHEN** a `GatewayConfig` has `node_discovery` set to `{ name: "nonexistent", typed_config: { ... } }`
- **AND** the gateway starts and looks up `"nonexistent"` in `Registry<NodeDiscoveryFactory>`
- **THEN** the factory is not found
- **AND** the gateway logs an error: "'nonexistent' not found"
- **AND** exits with status 1

### Requirement: Bazel extension pattern

The system SHALL provide a header-only `node_discovery_interface` Bazel target and a separate `cc_library` + `proto_library` per implementation (static, etcd, etc.). Users SHALL add their extension to `gateway/BUILD.bazel` deps, and `REGISTER_FACTORY` in the user's `.cc` SHALL auto-register at startup.

#### Scenario: Bazel targets compile

- **WHEN** building `//src/extensions/node_discovery/...`
- **THEN** the build succeeds with no errors
