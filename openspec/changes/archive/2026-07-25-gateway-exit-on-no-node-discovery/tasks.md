## 1. Gateway startup — error on missing node_discovery

- [x] 1.1 Replace the fallback block in `gateway.cc` with an error exit when `!config.has_node_discovery()`
- [x] 1.2 Replace the "factory not found" warning with an error exit
- [x] 1.3 Remove the hardcoded `127.0.0.1:9090` default address fallback
- [x] 1.4 Remove `#include "src/extensions/node_discovery/static/static_node_discovery.hh"` (no longer directly instantiated)
- [x] 1.5 Remove `--node_address` CLI flag and related code that appends to `node_connections`
- [x] 1.6 Check `UnpackTo` return value and error on failure (pre-existing gap exposed by removing fallback)
- [x] 1.7 Fix linker stripping: add `alwayslink` to `strij_cc_library` macro and set `alwayslink = True` on `static_node_discovery_lib`

## 2. Config validation — validate-only catches missing node_discovery

- [x] 2.1 Ensure the `--validate-only` path checks `node_discovery` presence (currently returns after config load — add the check before the early return)

## 3. Specs — update main spec files

- [x] 3.1 Update `openspec/specs/node-discovery/spec.md`: remove R5, replace S3/S5 with new error scenarios
- [x] 3.2 Update `openspec/specs/gateway-config/spec.md`: change validation rule 4

## 4. Config files — update defaults

- [x] 4.1 Update `config/gateway.yaml` (and `gateway.yaml` root copy) to use `node_discovery` instead of `node_connections`
- [x] 4.2 Verify `config/examples/gateway.yaml` already uses `node_discovery` (no change needed)

## 5. Tests

- [x] 5.1 Update or create a test for gateway startup with no `node_discovery` — expect error exit (validation in main(), tested via integration)
- [x] 5.2 Update gateway-config tests — spec test list already updated; config_loader tests unchanged (validation in gateway.cc, not ConfigLoader)
- [x] 5.3 Update node-discovery tests — no node_discovery tests exist to update

## 6. Build & verify

- [x] 6.1 Run `make build` — passes with no errors
- [x] 6.2 Run `make test` — all 4 tests pass
- [x] 6.3 Run `make clang-tidy` — no new lint issues (pre-existing protobuf header error only)
