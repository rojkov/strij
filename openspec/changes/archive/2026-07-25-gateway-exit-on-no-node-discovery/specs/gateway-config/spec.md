## MODIFIED Requirements

### REQ-GW-005: Cross-field validation catches common misconfigurations

The config loader SHALL validate cross-field constraints. The `node_discovery` field is now required. If `node_discovery` is not set in the config, the gateway SHALL fail validation. The `node_connections` field is ignored — it is retained in the protobuf schema for wire compatibility but no longer used for runtime behavior.

#### Scenario: Config without node_discovery fails validation
- **Given** a `GatewayConfig` with no `node_discovery` set
- **WHEN** the gateway validates the config
- **THEN** validation fails with an error indicating `node_discovery` is required

#### Scenario: Config with node_discovery and legacy node_connections
- **Given** a `GatewayConfig` with `node_discovery` set correctly
- **AND** `node_connections` also populated
- **WHEN** the gateway starts
- **THEN** `node_discovery` extension is used as configured
- **AND** `node_connections` is ignored (no error, no warning)

## MODIFIED Cross-Field Validation Rules

3. `request_timeout >= connection_timeout` (or both use defaults)
4. ~~At least one `node_connection` must be configured (warn if empty)~~
4. `node_discovery` extension MUST be configured (error if missing)
