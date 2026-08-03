## Why

The gateway and nodeagent currently have all configuration hardcoded (ports, addresses, log levels). Operators and CI/CD (Kubernetes) need declarative configuration files with schema validation. The schema will evolve across versions, and gateway/nodeagent will eventually exchange config over the wire — requiring a serialization format with schema evolution guarantees.

## What Changes

- Add **YAML configuration files** for gateway and nodeagent with protobuf-defined schemas
- Add **protobuf schema definitions** (`.proto` files) for all config types
- Add **config loading library** that parses YAML into validated C++ structs (generated from protobuf)
- Add **CLI flag overrides** (using Abseil Flags) for file path, environment variable overrides, and individual field overrides
- Update `gateway.cc` and `nodeagent.cc` to load config at startup instead of using hardcoded values
- Add **Bazel build rules** for protobuf compilation (rules_proto) and yaml-cpp dependency

### In Scope (v1 - Testable Now)
- HTTP/TLV listener address & port
- Logging: level, format, output (stdout/stderr), include_source_location
- Node connection addresses (gateway → nodeagent)
- Config file loading with layered overrides (defaults → YAML → env → CLI)
- Validation: required fields, ranges, enums, cross-field
- `--validate_only` mode

### Out of Scope (Future Work - Not Testable Yet)
- **TLS** - cert/key/ca files, verify_peer (no TLS implementation in codebase)
- **Timeouts** - connection_timeout, request_timeout, heartbeat_interval (not used by io layer)
- **Connection limits** - max_connections, reuse_port, read_buffer_size (not enforced)
- **Reconnect logic** - max_reconnect_attempts, reconnect_backoff_ms (NodeDirectory doesn't use)
- **File logging** - logging.output = "file" with file_path (not implemented)
- **Config hot-reload** - SIGHUP reload (future extension)

## Capabilities

### New Capabilities

- `yaml-config-loader`: Loads and validates YAML config files into protobuf-generated C++ structs, supports CLI/env overrides, provides clear validation errors
- `gateway-config`: Protobuf schema for gateway configuration (HTTP listener, node addresses, logging, **TLS/timeouts as reserved fields**)
- `nodeagent-config`: Protobuf schema for nodeagent configuration (TLV listener, logging, **TLS/timeouts as reserved fields**)

### Modified Capabilities

*(none - no existing capability specs define config loading behavior)*

## Impact

**Dependencies (new):**
- `protobuf` (via Bazel module) - schema definition and C++ codegen
- `yaml-cpp` - YAML parsing
- `rules_proto` - Bazel rules for protobuf compilation
- `abseil-cpp` (already present) - CLI flags, env var parsing

**Code changes:**
- `src/exe/gateway/gateway.cc` - load config, use config values (address, port, logging, node addresses)
- `src/exe/nodeagent/nodeagent.cc` - load config, use config values (address, port, logging)
- New: `src/core/config/` - config loading library
- New: `src/core/config/proto/` - .proto files and generated code
- `MODULE.bazel` - add protobuf, yaml-cpp, rules_proto deps
- `BUILD.bazel` files - proto_library, cc_library rules

**Runtime behavior:**
- Gateway/nodeagent fail fast with clear errors if config invalid
- Config file path via `--config_file` flag (default: `gateway.yaml` / `nodeagent.yaml`)
- Env var overrides: `STRIJ_GATEWAY_HTTP_PORT=8081`
- CLI overrides: `--gateway.http_port=8081`
- TLS/timeout fields parsed and validated but **not used** at runtime (reserved for future)