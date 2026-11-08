# Proposal

## Why

Running Strij locally today means hand-writing one YAML per process and starting
gateways and node agents by hand in the right order (see
`docs/testing-openworkflow.md`). The e2e harness already starts real clusters,
but it is test-scoped: ephemeral ports, a temporary directory, no CLI, and it
tears everything down when the test binary exits. There is no supported way to
leave a small cluster running while developing, and no convenient way to watch
its processes' logs when something goes wrong.

Recent work made this gap bigger: probe scheduling, the event plane, and
`emit`/`listen` all need several correctly wired processes to exercise. Manual
configs drift, boot order is easy to get wrong (a gateway that fails its first
node dial never re-dials), and a stray process keeps its port.

## What Changes

- Add `tools/dev_cluster.py`, a stdlib-only developer tool with `scaffold`,
  `up`, `down`, `status`, `logs`, and `list` commands, plus `make dev_*`
  wrappers.
- Commit preset cluster definitions under `config/examples/<name>/`: a
  `cluster.yaml` manifest plus one YAML per process. Presets are authored once
  (or generated once by `scaffold`) and read at runtime; `up` never rewrites
  them.
- Add `scaffold`, a one-time generator that writes a preset for a requested
  topology (`--nodes`, `--gateways`, ports, handlers, scheduler policy, event
  plane) and refuses to overwrite an existing preset without `--force`.
- Keep runtime state under `.devcluster/<name>/`: per-process log files and a
  `state.json` with pids, ports, and log paths. Add `.devcluster/` to
  `.gitignore`.
- Boot strictly nodes-first (each listener accepting) then gateways one at a
  time, proving readiness through the request path; abort with log tails when a
  process dies during boot. `down` signals process groups, escalates after a
  grace period, verifies ports are released, and keeps the stopped state for
  postmortem.
- `logs` prints a merged, name-prefixed tail across processes and can follow
  (`-f`) or filter to one process. Follow mode also watches process liveness and
  interleaves a marker when a process exits, calling out an unexpected exit
  (the cluster is not stopping) separately from a normal stop.
- Default the generated presets to `probe` scheduling (gateway `probe`,
  nodes advertising `probe` and `push` with `default` as local authority),
  `debug` logging, and the bundled `summary_mesh` event plane.
- Remove the unconditional debug `printf` in
  `src/common/core/logging/log_frontend.cc` that writes
  `LogFrontend::HandleCompletion called...` into every log file.
- Document the workflow next to the existing manual instructions.

## Capabilities

### New Capabilities

- `dev-cluster`: local cluster presets, lifecycle (scaffold/up/down/status),
  and merged log viewing for developer use.

### Modified Capabilities

None. The runtime protocol, scheduling, and event-plane behavior are unchanged;
the tool drives the existing binaries.

## Impact

- New: `tools/dev_cluster.py`, preset directories under `config/examples/`.
- Modified: `Makefile` (dev targets), `.gitignore` (`.devcluster/`),
  `src/common/core/logging/log_frontend.cc` (delete the debug `printf`),
  `docs/testing-openworkflow.md` (point at the tool).
- Unchanged: gateway/nodeagent binaries, config protos, e2e harness.
- No new build targets and no third-party Python dependencies.
