# YAML Config Loader Capability

## Overview
Library for loading, validating, and merging YAML configuration files into protobuf-generated C++ structs with support for environment variable and CLI flag overrides.

## v1 Scope (Testable Now)
- Parse YAML → protobuf message
- Validate: required fields, ranges, enums, patterns
- Merge layers: Defaults → YAML → Env vars → CLI flags
- Clear error messages with file:line:column references
- Thread-safe, no global state

## Out of Scope (Future Work)
- Config hot-reload (SIGHUP)
- Config encryption/secrets
- Remote config (etcd, Consul)
- Config versioning/migration
- Schema documentation generator
- JSON config format

## Public API

### Header: `carrot/config/config_loader.hh`

```cpp
#pragma once

#include <string>
#include <optional>
#include <vector>
#include "google/protobuf/message.h"

namespace carrot::config {

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

} // namespace carrot::config
```

## Configuration Layers (Priority Order)

```
1. Defaults (compile-time, from protobuf default values)
       │
       ▼
2. YAML File (--config_file or default path)
       │
       ▼
3. Environment Variables (CARROT_<SERVICE>_<FIELD_PATH>)
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
CARROT_<SERVICE>_<FIELD_PATH>
```

Rules:
- Service: `GATEWAY` or `NODEAGENT` (uppercase)
- Field path: protobuf field path with `_` as separator, `__` for array index
- Boolean: "true"/"false", "1"/"0", "yes"/"no" (case-insensitive)
- Duration: ISO 8601 or integer seconds (e.g., "30", "30s")
- Array index: `__0__`, `__1__`, etc.

Examples:
```
CARROT_GATEWAY_HTTP_LISTENER_PORT=8081
CARROT_GATEWAY_NODE_CONNECTIONS__0__ADDRESS=10.0.0.1:9090
CARROT_GATEWAY_LOGGING_LEVEL=debug
CARROT_NODEAGENT_TLV_LISTENER_PORT=9090
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

### Required Fields
- Marked with `option (carrot.config.required) = true` in protobuf (custom option)
- Or: non-zero default for non-optional fields

### Field Constraints
| Constraint | Protobuf Option | Example |
|------------|-----------------|---------|
| Range (min/max) | `carrot.config.range = {min: 1, max: 65535}` | Port numbers |
| Enum values | `carrot.config.enum_values = ["trace", "debug", "info"]` | Log levels |
| String pattern | `carrot.config.pattern = "^[a-z]+$"` | Identifiers |
| Required | `carrot.config.required = true` | Node address |

### Cross-Field Validation
- `heartbeat_interval <= connection_timeout`
- `tls.enabled → cert_file && key_file non-empty`
- `logging.output == "file" → logging.file_path non-empty`

## Error Reporting

### ConfigLoadResult Fields
- `success`: false on any error
- `error_message`: "Field 'port': value 70000 exceeds maximum 65535"
- `error_file`: Path to YAML file (or "<env>" or "<cli>")
- `error_line`: Line number in YAML (1-indexed)
- `error_column`: Column number
- `warnings`: ["Field 'deprecated_field' is deprecated"]

### Example Errors
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

### Dependencies
- `yaml-cpp` (YAML parsing)
- `absl/flags` (CLI parsing)
- `google/protobuf` (message reflection, validation)
- `absl/strings` (parsing helpers)

### Files
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

### Protobuf Custom Options (in `carrot/config/options.proto`)
```protobuf
syntax = "proto3";
package carrot.config;

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

### Usage in .proto files
```protobuf
message HttpListener {
  string address = 1 [default = "0.0.0.0"];
  uint32 port = 2 [
    default = 8081,
    (carrot.config.required) = true,
    (carrot.config.range_min) = "1",
    (carrot.config.range_max) = "65535"
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
carrot::config::ConfigLoadResult result = 
    carrot::config::LoadConfig<carrot::config::GatewayConfig>(
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

## Testing Requirements
- Unit tests for each parser (YAML, env, CLI)
- Integration tests: layer merging priority
- Validation tests: all constraint types
- Error message format tests
- Duration parsing tests (various formats)
- Edge cases: empty arrays, missing files, malformed YAML

## Performance
- Config loaded once at startup
- No runtime overhead after load
- YAML parsing: O(file size)
- Validation: O(field count)
- Memory: ~2x config size during load

## Thread Safety
- `LoadConfig` is thread-safe (no global state)
- Generated protobuf messages are thread-safe for read-only access
- Validation is pure function