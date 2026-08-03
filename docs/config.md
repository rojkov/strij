# Configuration

Gateway and NodeAgent use YAML configuration files with protobuf-defined schemas. Configuration is loaded at startup with layered overrides.

## Configuration Layers (Priority: low → high)

1. **Compile-time defaults** — protobuf field defaults
2. **YAML file** — path specified via `--config_file` (default: `gateway.yaml` / `nodeagent.yaml`)
3. **Environment variables** — `STRIJ_<SERVICE>_<FIELD_PATH>`
4. **CLI flags** — `--<field>=<value>`

Higher layers override lower ones.

## Gateway Config

### YAML File (`gateway.yaml`)

```yaml
http_listener:
  address: "0.0.0.0"     # default: "0.0.0.0"
  port: 8081              # range: 1-65535

node_connections:
  - address: "127.0.0.1:9090"   # format: host:port

logging:
  level: "info"           # trace|debug|info|warn|error
  format: "text"          # text|json
  output: "stdout"        # stdout|stderr
  include_source_location: false
```

### CLI Flags

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `--config_file` | string | `gateway.yaml` | Path to YAML config file |
| `--validate_only` | bool | `false` | Validate config and exit |
| `--http_port` | uint32 | `0` | Override HTTP listener port |
| `--http_address` | string | `""` | Override HTTP listener address |
| `--log_level` | string | `""` | Override log level |
| `--log_format` | string | `""` | Override log format |
| `--node_address` | string | `""` | Add node connection address (repeatable) |

### Environment Variables

| Variable | Description |
|----------|-------------|
| `STRIJ_GATEWAY_HTTP_LISTENER_PORT` | Override HTTP listener port |
| `STRIJ_GATEWAY_HTTP_LISTENER_ADDRESS` | Override HTTP listener address |
| `STRIJ_GATEWAY_NODE_CONNECTIONS__0__ADDRESS` | Set first node connection address |
| `STRIJ_GATEWAY_NODE_CONNECTIONS__1__ADDRESS` | Set second node connection address |
| `STRIJ_GATEWAY_LOGGING_LEVEL` | Override log level |
| `STRIJ_GATEWAY_LOGGING_FORMAT` | Override log format |
| `STRIJ_GATEWAY_LOGGING_OUTPUT` | Override log output |
| `STRIJ_GATEWAY_LOGGING_INCLUDE_SOURCE_LOCATION` | Override include source location |

Array indices are specified with double underscores: `__N__`.

## NodeAgent Config

### YAML File (`nodeagent.yaml`)

```yaml
tlv_listener:
  address: "0.0.0.0"     # default: "0.0.0.0"
  port: 9090              # range: 1-65535

logging:
  level: "debug"          # trace|debug|info|warn|error
  format: "text"          # text|json
  output: "stdout"        # stdout|stderr
  include_source_location: false
```

### CLI Flags

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `--config_file` | string | `nodeagent.yaml` | Path to YAML config file |
| `--validate_only` | bool | `false` | Validate config and exit |
| `--port` | uint32 | `0` | Override TLV listener port |
| `--address` | string | `""` | Override TLV listener address |
| `--log_level` | string | `""` | Override log level |
| `--log_format` | string | `""` | Override log format |

### Environment Variables

| Variable | Description |
|----------|-------------|
| `STRIJ_NODEAGENT_TLV_LISTENER_PORT` | Override TLV listener port |
| `STRIJ_NODEAGENT_TLV_LISTENER_ADDRESS` | Override TLV listener address |
| `STRIJ_NODEAGENT_LOGGING_LEVEL` | Override log level |
| `STRIJ_NODEAGENT_LOGGING_FORMAT` | Override log format |
| `STRIJ_NODEAGENT_LOGGING_OUTPUT` | Override log output |

## Validation

Config is validated after loading. Validation rules:

| Field | Rule |
|-------|------|
| `http_listener.port` / `tlv_listener.port` | Required, range 1-65535 |
| `http_listener.address` / `tlv_listener.address` | Required, valid IP or hostname |
| `node_connections[N].address` | Required, format `host:port` |
| `logging.level` | One of: `trace`, `debug`, `info`, `warn`, `error` |
| `logging.format` | One of: `text`, `json` |
| `logging.output` | One of: `stdout`, `stderr` |

## Validate Only Mode

Use `--validate_only` to check config without starting the service:

```bash
./gateway --config_file gateway.yaml --validate_only
./nodeagent --config_file nodeagent.yaml --validate_only
```

Exit code 0 = valid, 1 = invalid.

## Reserved Fields (v2+)

The following fields are parsed but **not used** at runtime in v1:

- `connection_timeout`, `request_timeout`, `heartbeat_interval` (Duration)
- `max_connections`, `reuse_port`, `read_buffer_size`
- `max_reconnect_attempts`, `reconnect_backoff_ms`, `connect_timeout_ms`
- `tls.*` (TLS not yet implemented)
- `logging.output = "file"` + `file_path`
