# YAML Config Loader Capability

## Purpose

Library for loading, validating, and merging YAML configuration files into protobuf-generated C++ structs, with support for environment variable and CLI flag overrides and clear, position-aware error reporting.

## Requirements

### Requirement: Load YAML into a protobuf message

The system SHALL provide `LoadConfig<T>(config_file_path, cli_overrides, output)` that parses a YAML file into a protobuf message of type `T` and returns a `ConfigLoadResult`. On failure the result SHALL carry a human-readable `error_message`, the `error_file`, and 1-indexed `error_line`/`error_column`.

#### Scenario: Valid YAML loads into config

- **WHEN** `LoadConfig<GatewayConfig>("gateway.yaml")` is called with a well-formed YAML file
- **THEN** the returned result has `success == true`
- **AND** the output message SHALL contain the values from the YAML

#### Scenario: Malformed YAML reports location

- **WHEN** `LoadConfig` is called with a YAML file containing a syntax error at line 3, column 12
- **THEN** the returned result has `success == false`
- **AND** `error_file`, `error_line == 3`, and `error_column == 12` SHALL identify the location

### Requirement: Config layer merging priority

The system SHALL merge configuration layers in priority order: compile-time protobuf defaults, then YAML file, then environment variables, then CLI flags, with each layer overriding the previous.

#### Scenario: CLI overrides YAML which overrides defaults

- **WHEN** a YAML file sets `logging.level` to `"warn"` and the CLI passes `--log_level=debug` with the protobuf default `"info"`
- **THEN** the final config SHALL have `logging.level == "debug"`

### Requirement: Environment variable overrides

The system SHALL support environment variables of the form `STRIJ_<SERVICE>_<FIELD_PATH>` (with `_` as field separator and `__` for array indexes) overriding config values. Booleans SHALL accept `"true"`/`"false"`, `"1"`/`"0"`, `"yes"`/`"no"` (case-insensitive); durations SHALL accept ISO 8601 or integer seconds.

#### Scenario: Environment variable overrides a field

- **WHEN** `STRIJ_GATEWAY_LOGGING_LEVEL=debug` is set and the gateway config is loaded
- **THEN** the final config SHALL have `logging.level == "debug"`
- **AND** `STRIJ_GATEWAY_NODE_CONNECTIONS__0__ADDRESS=10.0.0.1:9090` SHALL set the first node connection address

### Requirement: CLI flag overrides

The system SHALL support CLI overrides of the form `--<field_path>=<value>` with `.` as the field path separator. Repeated fields SHALL be set by repeating the flag, booleans by `--flag=true`/`--flag=false`, and durations using the same formats as environment variables.

#### Scenario: Repeated CLI flags populate a repeated field

- **WHEN** `--node_address=10.0.0.1:9090 --node_address=10.0.0.2:9090` is passed
- **THEN** the final config SHALL contain both addresses in order

#### Scenario: Boolean CLI flag

- **WHEN** `--tls_enabled=true` is passed
- **THEN** the final config SHALL have `tls.enabled == true`

### Requirement: Schema validation constraints

The system SHALL validate fields against constraints declared via the custom protobuf options: `required`, `range_min`/`range_max`, `enum_values`, and `pattern`. Violations SHALL produce a `ConfigLoadResult` with `success == false` and a descriptive `error_message`.

#### Scenario: Range violation rejected

- **WHEN** a YAML file sets `http_listener.port` to 70000
- **THEN** validation SHALL fail with an error message like `Field 'http_listener.port': value 70000 exceeds maximum 65535`

#### Scenario: Invalid enum value rejected

- **WHEN** a YAML file sets `logging.level` to `"verbose"`
- **THEN** validation SHALL fail with an error message naming the invalid enum value

### Requirement: Cross-field validation

The system SHALL validate cross-field constraints: `heartbeat_interval <= connection_timeout`; `tls.enabled` implies non-empty `cert_file` and `key_file`; and `logging.output == "file"` implies non-empty `logging.file_path`.

#### Scenario: TLS enabled without certificates

- **WHEN** a config sets `tls.enabled == true` and leaves `cert_file`/`key_file` empty
- **THEN** validation SHALL fail with an error

### Requirement: Thread-safe and stateless loading

The system SHALL perform config loading and validation without global state, so `LoadConfig` is safe to call concurrently. Generated protobuf messages SHALL be safe for read-only access after load.

#### Scenario: Concurrent loads do not interfere

- **WHEN** two threads call `LoadConfig` with different files concurrently
- **THEN** both complete with results matching their respective inputs

## Public API

Header: `strij/config/config_loader.hh`

```cpp
#pragma once

#include <string>
#include <optional>
#include <vector>
#include "google/protobuf/message.h"

namespace strij::config {

// Result of config loading
struct ConfigLoadResult {
  bool success = false;
  std::string error_message;       // Human-readable error
  std::string error_file;          // File where error occurred
  int error_line = 0;              // Line number (1-indexed)
  int error_column = 0;            // Column number (1-indexed)
  std::vector<std::string> warnings; // Non-fatal issues
};

// Load config from YAML file with env/CLI overrides
// T must be a protobuf-generated message type with default instance
template <typename T>
ConfigLoadResult LoadConfig(
    const std::string& config_file_path,
    const std::vector<std::string>& cli_overrides = {},
    T* output = nullptr);

// Validate a protobuf message against schema constraints
// Checks: required fields, field ranges, enum values, cross-field constraints
template <typename T>
ConfigLoadResult ValidateConfig(const T& config);

// Merge CLI flag overrides (--field=value format) into config
// Returns number of overrides applied
template <typename T>
int ApplyCliOverrides(T& config, const std::vector<std::string>& overrides);

// Get default config (compile-time defaults from protobuf)
template <typename T>
T GetDefaultConfig();

// Convert config to YAML string for debugging/dump
template <typename T>
std::string ConfigToYaml(const T& config);

} // namespace strij::config
```

## Configuration Layers (Priority Order)

```
1. Defaults (compile-time, from protobuf default values)
       │
       ▼
2. YAML File (--config_file or default path)
       │
       ▼
3. Environment Variables (STRIJ_<SERVICE>_<FIELD_PATH>)
       │
       ▼
4. CLI Flags (--field=value)
       │
       ▼
   Final Config
```

## YAML Parsing Rules

- Uses `yaml-cpp` library
- Maps YAML scalars to protobuf fields by name (snake_case ↔ camelCase)
- Repeated fields: YAML sequences → protobuf repeated fields
- Map fields: YAML mappings → protobuf map fields
- Duration fields: ISO 8601 duration strings (e.g., "30s", "1m", "1h30m")
- Enum fields: String values matching protobuf enum names (case-insensitive)

## Environment Variable Format

```
STRIJ_<SERVICE>_<FIELD_PATH>
```

Rules:

- Service: `GATEWAY` or `NODEAGENT` (uppercase)
- Field path: protobuf field path with `_` as separator, `__` for array index
- Boolean: "true"/"false", "1"/"0", "yes"/"no" (case-insensitive)
- Duration: ISO 8601 or integer seconds (e.g., "30", "30s")
- Array index: `__0__`, `__1__`, etc.

Examples:

```
STRIJ_GATEWAY_HTTP_LISTENER_PORT=8081
STRIJ_GATEWAY_NODE_CONNECTIONS__0__ADDRESS=10.0.0.1:9090
STRIJ_GATEWAY_LOGGING_LEVEL=debug
STRIJ_NODEAGENT_TLV_LISTENER_PORT=9090
```

## CLI Override Format

```
--<field_path>=<value>
```

Rules:

- Field path uses `.` as separator (matches protobuf)
- Repeated fields: repeat flag (e.g., `--node_address=a --node_address=b`)
- Boolean: `--flag=true` or `--flag=false` or just `--flag` (true)
- Duration: same formats as env vars

Examples:

```
--http_port=8081
--log_level=debug
--node_address=10.0.0.1:9090 --node_address=10.0.0.2:9090
--tls_enabled=true
```

## Validation Rules

**Required fields:** marked with `option (strij.config.required) = true` in protobuf (custom option), or non-zero default for non-optional fields.

**Field constraints:**

| Constraint | Protobuf Option | Example |
|------------|-----------------|---------|
| Range (min/max) | `strij.config.range = {min: 1, max: 65535}` | Port numbers |
| Enum values | `strij.config.enum_values = ["trace", "debug", "info"]` | Log levels |
| String pattern | `strij.config.pattern = "^[a-z]+$"` | Identifiers |
| Required | `strij.config.required = true` | Node address |

**Cross-field validation:**

- `heartbeat_interval <= connection_timeout`
- `tls.enabled → cert_file && key_file non-empty`
- `logging.output == "file" → logging.file_path non-empty`

## Error Reporting

`ConfigLoadResult` fields:

- `success`: false on any error
- `error_message`: "Field 'port': value 70000 exceeds maximum 65535"
- `error_file`: Path to YAML file (or "<env>" or "<cli>")
- `error_line`: Line number in YAML (1-indexed)
- `error_column`: Column number
- `warnings`: ["Field 'deprecated_field' is deprecated"]

Example:

```
ConfigLoadResult{
  success=false,
  error_message="Field 'http_listener.port': value 70000 exceeds maximum 65535",
  error_file="gateway.yaml",
  error_line=3,
  error_column=12,
  warnings=[]
}
```

## Implementation Details

**Dependencies:**

- `yaml-cpp` (YAML parsing)
- `absl/flags` (CLI parsing)
- `google/protobuf` (message reflection, validation)
- `absl/strings` (parsing helpers)

**Files:**

```
src/core/config/
├── config_loader.hh      # Public API
├── config_loader.cc      # Implementation
├── yaml_parser.hh        # YAML → protobuf mapping
├── yaml_parser.cc
├── env_parser.hh         # Env var parsing
├── env_parser.cc
├── cli_parser.hh         # CLI override parsing
├── cli_parser.cc
├── validator.hh          # Schema validation
├── validator.cc
└── duration_parser.hh    # ISO 8601 duration parsing
```

**Protobuf custom options (in `strij/config/options.proto`):**

```protobuf
syntax = "proto3";
package strij.config;

import "google/protobuf/descriptor.proto";

extend google.protobuf.FieldOptions {
  bool required = 50000;
  string range_min = 50001;
  string range_max = 50002;
  repeated string enum_values = 50003;
  string pattern = 50004;
  string deprecated_message = 50005;
}
```

**Usage in .proto files:**

```protobuf
message HttpListener {
  string address = 1 [default = "0.0.0.0"];
  uint32 port = 2 [
    default = 8081,
    (strij.config.required) = true,
    (strij.config.range_min) = "1",
    (strij.config.range_max) = "65535"
  ];
}
```

## Integration with Abseil Flags

```cpp
// In gateway.cc / nodeagent.cc
ABSL_FLAG(std::string, config_file, "gateway.yaml", "Path to YAML config file");
ABSL_FLAG(bool, validate_only, false, "Validate config and exit");
ABSL_FLAG(uint32_t, http_port, 0, "Override HTTP listener port (0 = use config)");
ABSL_FLAG(std::string, log_level, "", "Override log level");

// After absl::ParseCommandLine()
strij::config::ConfigLoadResult result =
    strij::config::LoadConfig<strij::config::GatewayConfig>(
        absl::GetFlag(FLAGS_config_file),
        GetCliOverrides(),  // Convert absl flags to vector<string>
        &config);

if (!result.success) {
  LOG_ERROR() << "Config error: " << result.error_message
              << " at " << result.error_file << ":" << result.error_line;
  return 1;
}

if (absl::GetFlag(FLAGS_validate_only)) {
  LOG_INFO() << "Config validation passed";
  return 0;
}
```

## Out of Scope (Future Work)

- Config hot-reload (SIGHUP)
- Config encryption/secrets
- Remote config (etcd, Consul)
- Config versioning/migration
- Schema documentation generator
- JSON config format
