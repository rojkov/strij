## MODIFIED Requirements

### R4: GatewayConfig Integration
The `GatewayConfig` SHALL have a `node_discovery` field of type `ExtensionConfig`. The gateway SHALL look up `node_discovery.name()` in `Registry<NodeDiscoveryFactory>`, unpack `typed_config` into the factory's expected config type, and call `factory->create(unpacked_config, context)`. If `node_discovery` is not set, the gateway MUST exit with an error.

#### Scenario: Node discovery configured correctly
- **Given** a `GatewayConfig` with `node_discovery` set to `{ name: "static", typed_config: { addresses: ["10.0.0.1:9090"] } }`
- **WHEN** the gateway starts
- **THEN** the factory is looked up by name `"static"`
- **AND** `NodeDiscovery` is created and started successfully

#### Scenario: Node discovery missing from config
- **Given** a `GatewayConfig` with no `node_discovery` field set
- **WHEN** the gateway starts
- **THEN** the gateway SHALL log an error and exit with non-zero status

### R5: (REMOVED) Backward Compatibility

**Reason:** The extension mechanism is now the primary way to configure node discovery. Silent fallback to `node_connections` masked configuration errors. Users MUST explicitly configure a `node_discovery` extension.

**Migration:** Replace `node_connections` in config with `node_discovery: { name: "static", typed_config: { addresses: [...] } }`.

## REMOVED Requirements

### Requirement: R5: Backward Compatibility
**Reason:** The extension mechanism is now required — silent fallback to `node_connections` is removed.
**Migration:** Add `node_discovery` section to gateway config using a registered extension (e.g., `"static"`).

## ADDED Requirements

### Requirement: Required node_discovery extension
The gateway SHALL fail on startup if `config.has_node_discovery()` is false. This check MUST happen after config loading and CLI override application, before creating the event loop. The error message MUST clearly indicate that `node_discovery` is required and suggest adding a static discovery section.

#### Scenario: Validate-only with missing node_discovery
- **Given** a `GatewayConfig` with no `node_discovery` field set
- **WHEN** `--validate-only` flag is passed
- **THEN** the gateway SHALL report the error and exit with non-zero status

### Requirement: Error on unregistered extension name
If `config.has_node_discovery()` is true but the extension name is not found in the factory registry, the gateway MUST exit with an error. The error message MUST name the missing extension and list available extensions if the registry supports enumeration.

#### Scenario: Unregistered discovery extension causes error
- **Given** a `GatewayConfig` with `node_discovery` set to `{ name: "nonexistent" }`
- **WHEN** the gateway starts
- **THEN** the gateway SHALL log an error indicating `"nonexistent"` is not registered
- **AND** exit with non-zero status

## REMOVED Scenarios

### Scenario: S3: Fallback to legacy node_connections
**Reason:** Fallback removed — `node_discovery` is now required.
**Migration:** Use `node_discovery` extension (e.g., `{ name: "static", typed_config: { addresses: ["10.0.0.1:9090"] } }`).

### Scenario: S5: Unregistered discovery extension
**Reason:** Unregistered extensions now cause a hard error instead of falling back.
**Migration:** Ensure the extension is linked into the binary and the name in config matches the registered name.
