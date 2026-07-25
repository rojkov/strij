## Why

When `node_discovery` is omitted from the gateway config, the gateway silently falls back to legacy `node_connections[]` (or a hardcoded `127.0.0.1:9090` default). This masks configuration errors — users deploying without an extension never realize their node discovery is broken. The extension mechanism is now the primary way to configure node discovery; omitting it should be a hard failure.

## What Changes

- **BREAKING**: Remove the `node_connections` fallback in `gateway.cc`. If `node_discovery` extension is not configured, the gateway exits with a clear error message.
- **BREAKING**: Remove the hardcoded `127.0.0.1:9090` default address fallback.
- Update `node-discovery` spec (remove backward-compatibility requirement R5, change S3/S5).
- Update `gateway-config` spec (change validation rule 4 from "warn on empty node_connections" to "error if no node_discovery configured").
- Update config examples/docs to show `node_discovery` as required.

## Capabilities

### New Capabilities

- *(none)*

### Modified Capabilities

- `node-discovery`: Remove backward-compatibility fallback to `node_connections`. The `node_discovery` extension field is now required in GatewayConfig. Scenarios S3 (fallback via node_connections) and S5 (unregistered extension falls back) are removed; new error scenarios replace them.
- `gateway-config`: Change cross-field validation rule 4 to require `node_discovery` to be present. Remove the "empty node_connections → warning" rule.

## Impact

- `src/exe/gateway/gateway.cc`: Rewrite the node discovery initialization block — remove fallback, add error exit.
- `openspec/specs/node-discovery/spec.md`: R5 removed, S3/S5 replaced with error scenarios.
- `openspec/specs/gateway-config/spec.md`: Validation rule 4 changed, test list updated.
- `config/examples/gateway.yaml`: No change (already uses `node_discovery`).
- `config/gateway.yaml` and `gateway.yaml`: These use legacy `node_connections` — they must be updated or users must add `node_discovery`. Document the breaking change.
