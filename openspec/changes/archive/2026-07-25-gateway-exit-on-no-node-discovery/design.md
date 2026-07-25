## Context

The gateway currently has three paths for node discovery:

1. **Extension path** — `config.has_node_discovery()` → lookup factory → create discovery instance.
2. **Fallback path** — If no extension loaded, iterate `config.node_connections()` and build a `StaticNodeDiscovery` from those addresses.
3. **Default path** — If both are empty/missing, hardcode `127.0.0.1:9090`.

Paths 2 and 3 silently mask misconfiguration. The extension mechanism is now stable and the only intended way to configure node discovery.

## Goals / Non-Goals

**Goals:**
- Gateway exits with a clear error if `node_discovery` is omitted from config (including `--validate-only` mode).
- Gateway exits with a clear error if `node_discovery` is configured but the factory name is not registered.
- Remove the `node_connections` fallback and the hardcoded default address.
- Update the `--node_address` CLI flag to be an error if used without `node_discovery` (or remove it).

**Non-Goals:**
- Not removing `node_connections` from the protobuf schema (preserve wire compat for older configs; field remains but is ignored).
- Not changing how `StaticNodeDiscovery` works — it's still used when the `"static"` extension is explicitly configured.
- Not retrofitting other extensions or adding new discovery backends.

## Decisions

### D1: Error on absent `node_discovery`, not on missing extension registration

If `config.has_node_discovery()` is false → error immediately, before the registry lookup. If it's true but the factory name is missing from the registry → error too (different message). This distinguishes "user forgot to configure discovery entirely" from "user mistyped the extension name or forgot to link the library".

### D2: `--node_address` CLI flag behavior

The `--node_address` flag currently appends to `config.node_connections()`. Since `node_connections` will become a no-op, this flag can either be removed or converted to populate a `--node_discovery`-style config. Since there's no CLI extension config builder yet, **remove the `--node_address` flag** (breaking change) and require users to use the config file.

### D3: Validation moved to config validation phase

The check belongs in config loading/validation so `--validate-only` catches it. However, the existing `ConfigLoader` is protobuf-driven and doesn't have extension awareness. Two options:

- **Option A (chosen)**: Validate in `gateway.cc` after loading config, before creating the event loop. Simple, clear, no changes to `ConfigLoader`. Works with `--validate-only` since the existing `--validate-only` returns after loading.
- **Option B**: Add cross-field validation to `ConfigLoader`. More architecturally pure but requires plumbing extension registry knowledge into the loader, which is a bigger refactor.

Chosen: **Option A** — add the check inline in `gateway.cc` after config loading and CLI override application.

```cpp
if (!config.has_node_discovery()) {
  LOG_ERROR("Config error: node_discovery extension is required. "
            "Configure a node discovery extension (e.g., 'static') in your config file.");
  return 1;
}
```

## Risks / Trade-offs

- **Breaking change**: Existing configs using only `node_connections` will fail to start. Users must add a `node_discovery` section. Mitigation: clear error message with example config snippet.
- **`--node_address` flag removal**: Scripts and deployments using this flag will break. Mitigation: flag already had a note saying "would need repeated flag support" — unlikely to be heavily used. Document in release notes.
- **`gateway.yaml` in repo root**: The default dev config uses legacy `node_connections` — must be updated to use `node_discovery: { name: "static", ... }` so `carrot` still works out of the box.
