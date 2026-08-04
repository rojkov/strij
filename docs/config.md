# Configuration

Gateway and NodeAgent use YAML configuration files with protobuf-defined schemas. Configuration is loaded at startup with layered overrides.

## Configuration Layers (Priority: low → high)

1. **Compile-time defaults** — protobuf field defaults
2. **YAML file** — path specified via `--config_file` (default: `gateway.yaml` / `nodeagent.yaml`)
3. **Environment variables** — `STRIJ_<SERVICE>_<FIELD_PATH>`
4. **CLI flags** — service-specific flags (e.g. `--http_port`, `--log_level`)

Higher layers override lower ones.

## Gateway Config

### YAML File (`gateway.yaml`)

```yaml
http_listener:
  address: "0.0.0.0"     # default: "0.0.0.0"
  port: 8081              # range: 1-65535

node_discovery:
  name: "static"
  typed_config:
    "@type": "type.googleapis.com/strij.config.StaticNodeDiscoveryConfig"
    addresses:
      - "127.0.0.1:9090"

logging:
  level: "info"           # trace|debug|info|warn|error
  format: "text"          # text|json
  output: "stdout"        # stdout|stderr
  include_source_location: false
```

The `node_discovery` extension is **required**; the gateway exits if it is absent. `name` selects the registered node discovery factory (e.g. `static`); `typed_config` carries the factory-specific configuration unpacked from the `@type` message.

### CLI Flags

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `--config_file` | string | `gateway.yaml` | Path to YAML config file |
| `--validate_only` | bool | `false` | Validate config and exit |
| `--http_port` | uint32 | `0` | Override HTTP listener port |
| `--http_address` | string | `""` | Override HTTP listener address |
| `--log_level` | string | `""` | Override log level |
| `--log_format` | string | `""` | Override log format |

### Environment Variables

| Variable | Description |
|----------|-------------|
| `STRIJ_GATEWAY_HTTP_LISTENER_PORT` | Override HTTP listener port |
| `STRIJ_GATEWAY_HTTP_LISTENER_ADDRESS` | Override HTTP listener address |
| `STRIJ_GATEWAY_NODE_DISCOVERY_NAME` | Override node discovery extension name |
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

task_handlers:
  - name: "echo"
    typed_config:
      "@type": "type.googleapis.com/strij.extensions.task_handlers.echo.EchoTaskHandlerConfig"

logging:
  level: "debug"          # trace|debug|info|warn|error
  format: "text"          # text|json
  output: "stdout"        # stdout|stderr
  include_source_location: false
```

`task_handlers` is a list of extension configs. Each `name` must match a registered task handler factory; unknown names cause startup to fail. If no handlers are configured, all incoming tasks are dropped.

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
| `STRIJ_NODEAGENT_TASK_HANDLERS__0__NAME` | Set first task handler extension name |
| `STRIJ_NODEAGENT_LOGGING_LEVEL` | Override log level |
| `STRIJ_NODEAGENT_LOGGING_FORMAT` | Override log format |
| `STRIJ_NODEAGENT_LOGGING_OUTPUT` | Override log output |
| `STRIJ_NODEAGENT_LOGGING_INCLUDE_SOURCE_LOCATION` | Override include source location |

Array indices are specified with double underscores: `__N__`.

## Validation

Config is validated after loading. Validation rules:

| Field | Rule |
|-------|------|
| `http_listener.port` / `tlv_listener.port` | Required, range 1-65535 |
| `logging.level` | One of: `trace`, `debug`, `info`, `warn`, `error` |
| `logging.format` | One of: `text`, `json` |
| `logging.output` | One of: `stdout`, `stderr` |

Extension configs are checked at startup: the gateway requires a `node_discovery` section whose `name` matches a registered `NodeDiscoveryFactory`, and each nodeagent `task_handlers[N].name` must match a registered `TaskHandlerFactory`. These checks run even in `--validate_only` mode.

## Validate Only Mode

Use `--validate_only` to check config without starting the service:

```bash
./gateway --config_file gateway.yaml --validate_only
./nodeagent --config_file nodeagent.yaml --validate_only
```

Exit code 0 = valid, 1 = invalid.

## Reserved Fields (v2+)

The following fields are reserved for future use and **not used** at runtime in v1:

- `http_listener.max_connections`, `http_listener.reuse_port`
- `tlv_listener.max_connections`, `tlv_listener.reuse_port`, `tlv_listener.read_buffer_size`
- `connection_timeout`, `heartbeat_interval` (Duration)
- `tls.*` (TLS not yet implemented)
- `logging.output = "file"` + `logging.file_path`
