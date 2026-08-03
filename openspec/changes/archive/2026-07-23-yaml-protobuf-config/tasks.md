# Tasks: YAML + Protobuf Config for Gateway & NodeAgent

## Scope: v1 (Testable Now)

**In Scope:**
- HTTP/TLV listener address & port
- Logging: level, format, output (stdout/stderr), include_source_location
- Node connection addresses (gateway → nodeagent)
- Config file loading with layered overrides (defaults → YAML → env → CLI)
- Validation: required fields, ranges, enums, cross-field
- `--validate_only` mode

**Out of Scope (Future Work - Not Testable Yet):**
- TLS (cert/key/ca, verify_peer) — no TLS implementation in codebase
- Timeouts (connection_timeout, request_timeout, heartbeat_interval) — not used by io layer
- Connection limits (max_connections, reuse_port, read_buffer_size) — not enforced
- Reconnect logic (max_reconnect_attempts, reconnect_backoff_ms) — NodeDirectory doesn't use
- File logging (logging.output = "file" with file_path) — not implemented
- Config hot-reload, config via TLV, config versioning, secrets integration

---

## Phase 1: Dependencies & Build System

### Task 1.1: Add Bazel Dependencies
- [x] Add `protobuf` module to `MODULE.bazel` (version 33.4)
- [x] Add `rules_proto` module to `MODULE.bazel` (version 7.1.0)
- [x] Add `yaml-cpp` module to `MODULE.bazel` (version 0.8.0)
- [x] Run `bazel build //...` to fetch dependencies
- [x] Verify `@protobuf//...` and `@rules_proto//...` repositories available

### Task 1.2: Add Protobuf Build Rules
- [x] Create `src/core/config/proto/BUILD.bazel` with:
  - `proto_library` for `gateway.proto`
  - `proto_library` for `nodeagent.proto`
  - `proto_library` for `options.proto`
  - `cc_proto_library` for generated C++ code
- [x] Test build: `bazel build //src/core/config/proto:config_cc_proto`

## Phase 2: Protobuf Schema Definitions

### Task 2.1: Create Custom Validation Options
- [x] Create `src/core/config/proto/options.proto` with:
  - `extend google.protobuf.FieldOptions` for `required`, `range_min`, `range_max`, `enum_values`, `pattern`, `deprecated_message`
  - Shared `Logging` message with validation annotations

### Task 2.2: Create Gateway Protobuf Schema
- [x] Create `src/core/config/proto/gateway.proto` with:
  - `GatewayConfig`, `HttpListener`, `NodeConnection`
  - **v1 fields only**: `HttpListener.address`, `HttpListener.port`, `NodeConnection.address`
  - **Reserved fields (commented as v2+)**
  - Validation annotations: `required`, `range`, `pattern`

### Task 2.3: Create NodeAgent Protobuf Schema
- [x] Create `src/core/config/proto/nodeagent.proto` with:
  - `NodeAgentConfig`, `TlvListener`
  - **v1 fields only**: `TlvListener.address`, `TlvListener.port`
  - **Reserved fields (commented as v2+)**
  - Validation annotations: `required`, `range`

### Task 2.4: Verify Protobuf Generation
- [x] Build protobuf libraries: `bazel build //src/core/config/proto:all`
- [x] Verify generated `.pb.h` and `.pb.cc` files exist
- [x] Check generated code compiles with C++23

## Phase 3: Config Loader Library

### Task 3.1: Create Config Loader Interface
- [x] Create `src/core/config/config_loader.hh` with:
  - `ConfigLoadResult` struct (success, error_message, error_file, error_line, error_column, warnings)
  - `LoadConfig<T>()`, `ValidateConfig<T>()`, `ApplyCliOverrides<T>()` template functions

### Task 3.2: Implement YAML Parser
- [x] Implement in `config_loader.cc`:
  - `ParseYamlFile()` → `YAML::Node`
  - `MergeYamlIntoProto()` using protobuf reflection
  - Handle repeated fields, nested messages

### Task 3.3: Implement Environment Variable Parser
- [x] Create `src/core/config/env_parser.hh/cc`:
  - `GetEnvOverrides(prefix)` → map of field paths to values
  - Prefix convention: `STRIJ_GATEWAY_` / `STRIJ_NODEAGENT_`
  - Nested field mapping: `HTTP_LISTENER__PORT` → `http_listener.port`
  - Repeated field indexing: `NODE_CONNECTIONS__0__ADDRESS` → `node_connections[0].address`
  - Type conversion: string → protobuf field type

### Task 3.4: Implement CLI Flag Parser
- [x] Implement in `config_loader.cc`:
  - `ApplyCliOverridesInternal()` using path-based field traversal
  - Supports dot-separated field paths
  - Type conversion: string → protobuf field type

### Task 3.5: Implement Validation Engine
- [x] Implement in `config_loader.cc`:
  - `ValidateMessage()` using protobuf reflection + custom protobuf options
  - Required field checks (from `(strij.config.required)` option)
  - Range validation (from `(strij.config.range_min)` / `(strij.config.range_max)` options)
  - Enum value validation (from `(strij.config.enum_values)` option)
  - Regex pattern validation (from `(strij.config.pattern)` option)
  - Collect all errors, return structured result

### Task 3.6: Implement Config Loader Integration
- [x] Create `src/core/config/config_loader.cc`:
  - Layer merging: defaults → YAML → env → CLI
  - Priority handling (higher wins)
  - Warning collection (missing optional fields, deprecated fields, reserved fields used)
  - Error context with file:line:column

### Task 3.7: Create Config Loader BUILD Rule
- [x] Create `src/core/config/BUILD.bazel`:
  - `strij_cc_library` for `config_loader_lib`
  - Dependencies: protobuf CC libs, yaml-cpp, absl/flags, absl/strings
  - Visibility for gateway and nodeagent targets

### Task 3.8: Unit Tests for Config Loader
- [x] Create `test/core/config/config_loader_test.cc`:
  - Test each parser independently
  - Test layer merging priority
  - Test validation: required, range, enum, pattern
  - Test error message format
  - Test CLI overrides
  - Test env var overrides
  - Test repeated field handling (env var array indices)
  - Test validate-only mode
  - Build and run: 15 tests passing

## Phase 4: Gateway Integration

### Task 4.1: Update Gateway Main
- [x] Modify `src/exe/gateway/gateway.cc`:
  - Add Abseil flags: `config_file`, `validate_only`, common overrides (`http_port`, `http_address`, `log_level`, `log_format`, `node_address` repeatable)
  - Call `config::LoadGatewayConfig()` at startup
  - Handle `validate_only` flag (exit 0 on success)
  - Use config values for:
    - `TcpListener` port/address
    - `NodeDirectory` addresses
    - Logger level/format/output
  - Remove hardcoded values

### Task 4.2: Update Gateway BUILD
- [x] Update `src/exe/gateway/BUILD.bazel`:
  - Add dependency on `//src/core/config:config_loader_lib`
  - Add dependency on `//src/core/config/proto:config_cc_proto`

### Task 4.3: Create Default Gateway Config
- [x] Create `config/gateway.yaml` with documented defaults
- [x] Add example in `config/examples/gateway.yaml`
- [x] `config/examples/kubernetes-configmap.yaml`

### Task 4.4: Gateway Integration Tests
- [ ] Test: Gateway starts with default config (no file)
- [ ] Test: Gateway loads `gateway.yaml`
- [ ] Test: Gateway loads config with env overrides
- [ ] Test: Gateway loads config with CLI overrides
- [ ] Test: Gateway `--validate_only` success/failure
- [ ] Test: Invalid config → clear error, exit 1

## Phase 5: NodeAgent Integration

### Task 5.1: Update NodeAgent Main
- [x] Modify `src/exe/nodeagent/nodeagent.cc`:
  - Add Abseil flags: `config_file`, `validate_only`, common overrides (`port`, `address`, `log_level`, `log_format`)
  - Call `config::LoadNodeAgentConfig()` at startup
  - Handle `validate_only` flag
  - Use config values for:
    - `TcpListener` port/address
    - Logger level/format/output
  - Remove hardcoded values

### Task 5.2: Update NodeAgent BUILD
- [x] Update `src/exe/nodeagent/BUILD.bazel`:
  - Add dependency on `//src/core/config:config_loader_lib`
  - Add dependency on `//src/core/config/proto:config_cc_proto`

### Task 5.3: Create Default NodeAgent Config
- [x] Create `config/nodeagent.yaml` with documented defaults
- [x] Add example in `config/examples/nodeagent.yaml`

### Task 5.4: NodeAgent Integration Tests
- [ ] Test: NodeAgent starts with default config
- [ ] Test: NodeAgent loads `nodeagent.yaml`
- [ ] Test: NodeAgent loads config with env overrides
- [ ] Test: NodeAgent loads config with CLI overrides
- [ ] Test: NodeAgent `--validate_only` success/failure
- [ ] Test: Invalid config → clear error, exit 1

## Phase 6: Documentation & Examples

### Task 6.1: Create Config Documentation
- [x] Create `docs/config.md` with:
  - Configuration file format reference
  - **v1 fields** with types, defaults, constraints
  - **Reserved fields** marked as v2+ (not used in v1)
  - Environment variable reference
  - CLI flag reference
  - Kubernetes ConfigMap/Secret examples (v1 fields only)
  - TLS configuration guide marked as future

### Task 6.2: Add Example Configs
- [x] Create `config/examples/gateway.yaml` (v1 fields only)
- [x] Create `config/examples/nodeagent.yaml` (v1 fields only)
- [x] Create `config/examples/kubernetes-configmap.yaml`

### Task 6.3: Update README
- [x] Add configuration section to `README.md`
- [x] Document `--config_file`, `--validate_only` flags
- [x] Link to `docs/config.md`

## Phase 7: Verification

### Task 7.1: Full Build Test
- [x] `bazel build //src/exe/gateway:gateway`
- [x] `bazel build //src/exe/nodeagent:nodeagent`
- [x] `bazel test //test/...` (all existing tests pass)

### Task 7.2: Manual Integration Test
- [ ] Start nodeagent with config file
- [ ] Start gateway with config file pointing to nodeagent
- [ ] Verify HTTP request → TLV → gateway → nodeagent → response flow
- [ ] Test config validation with invalid values
- [ ] Test env var overrides
- [ ] Test CLI flag overrides

### Task 7.3: Sanitizer Tests
- [ ] `make test_asan` - AddressSanitizer
- [ ] `make test_tsan` - ThreadSanitizer

---

## Future Work (Phase 8+ - Separate Change)

When the codebase implements these features, create a follow-up change:

### TLS Support
- [ ] Add TLS to TcpListener (OpenSSL/boringssl integration)
- [ ] Enable TlsConfig fields in protobuf
- [ ] Add cert/key loading validation
- [ ] Kubernetes Secret examples

### Timeouts & Connection Limits
- [ ] Implement connection_timeout in TcpListener/NodeDirectory
- [ ] Implement request_timeout in HTTP handler
- [ ] Implement heartbeat_interval in nodeagent TLV handler
- [ ] Enforce max_connections in TcpListener
- [ ] Implement SO_REUSEPORT in TcpListener
- [ ] Use read_buffer_size in TLV parser

### Reconnect Logic
- [ ] Implement max_reconnect_attempts in NodeDirectory
- [ ] Implement reconnect_backoff_ms in NodeDirectory
- [ ] Implement connect_timeout_ms in NodeDirectory

### File Logging
- [ ] Implement logging.output = "file" with file_path
- [ ] Add log rotation

### Advanced Config Features
- [ ] Config hot-reload (SIGHUP)
- [ ] Config via TLV (gateway → nodeagent)
- [ ] Config versioning (v1 → v2 migration)
- [ ] Secrets integration (env:SECRET, file:/run/secrets/...)