# Tasks

## 1. Cleanup and runtime footprint

- [x] 1.1 Delete the unconditional debug `printf` in
  `src/common/core/logging/log_frontend.cc` that prints
  `LogFrontend::HandleCompletion called...`. Verify with
  `bazel build //src/gateway/exe:gateway //src/nodeagent/exe:nodeagent` and
  `./bazel-bin/src/gateway/exe/gateway --config_file=config/examples/gateway.yaml --validate_only`
  that no such line appears.
- [x] 1.2 Add `.devcluster/` to `.gitignore`. Verify by creating
  `.devcluster/probe/x` and confirming `git status --short` reports nothing for
  it, then removing the probe directory.

## 2. Preset model and runtime state

- [x] 2.1 Implement preset discovery and parsing in `tools/dev_cluster.py`:
  `list` finds `config/examples/*/cluster.yaml`, resolves each process's role and
  config, and reads the listener port from that config. A non-node/gateway role,
  missing config, or unreadable listener port fails naming the offending entry.
  Verify with `python3 tools/dev_cluster.py list` and the unit tests from 2.2.
- [x] 2.2 Add `tools/dev_cluster_test.py` (stdlib `unittest`) covering manifest
  parsing, both listener shapes, the missing-port error, and role validation.
  Verify `python3 tools/dev_cluster_test.py` passes.
- [x] 2.3 Implement `.devcluster/<name>/state.json` with atomic replacement, a
  per-cluster `flock`, and liveness classification (live, stale, stopped).
  Verify unit tests for a live pid (`os.getpid()`), an unused pid, and missing
  state, plus a write/read round-trip.

## 3. Scaffold and the committed preset

- [x] 3.1 Implement `scaffold` rendering: manifest, one config per process,
  every gateway's static discovery listing every node, `probe` gateway policy,
  node `probe`+`push`+`default` schedulers, `summary_mesh` with unique
  `gateway_id`s, `debug` logging, `--scheduler`, `--no-event-plane`, `--force`,
  and rejection of a port range used by another preset. Verify unit tests that
  render each topology and assert discovery lists, gateway ids, log level, and
  the collision error.
- [x] 3.2 Generate and commit `config/examples/local/` with the scaffold
  defaults (two nodes, one gateway, probe, event plane, debug). Verify
  `gateway --validate_only` and `nodeagent --validate_only` pass for every
  generated config and `python3 tools/dev_cluster.py list` reports the preset.
- [x] 3.3 Create `docs/dev-cluster.md` documenting the preset layout, manifest
  fields, scaffold defaults, `--scheduler`, and port-range convention. Verify
  the documented scaffold command reproduces the committed `local/` tree.

## 4. Start and status

- [x] 4.1 Implement `up`: port bind-checks, spawning with a new session, log
  truncation, repo-root cwd, nodes-first barrier, sequential gateway readiness
  through the manifest's readiness task, abort with the failing process's log
  tail and cleanup, refusal while a listed pid is live, stale-state replacement,
  and the summary with a curl hint. Verify `python3 tools/dev_cluster.py up local`
  serves `curl -d hello http://127.0.0.1:8081/tasks/echo` with `hello`, a second
  `up` refuses while live, a killed cluster's state is replaced on the next
  `up`, and a scratch preset with a node pool removed aborts with that node's
  log tail.
- [x] 4.2 Implement `status` for all clusters and for one named cluster, showing
  name, role, pid, port, running/stopped state, and log path, and failing on an
  unknown name while listing the known ones. Verify against a running cluster,
  after `down`, and with an unknown name.
- [x] 4.3 Extend `docs/dev-cluster.md` with the start and status workflow,
  including the pid as the debugger attach target. Verify the documented
  commands run as written.

## 5. Stop, logs, and cleanup

- [x] 5.1 Implement `down`: gateway-first SIGTERM to process groups, bounded
  grace, SIGKILL of stragglers, port-closed verification, retained state marked
  stopped, and a successful no-op when no state exists. Record `stopping_at`
  before the first signal so a concurrent viewer can classify exits. Verify a
  graceful stop leaves no cluster processes, every former port refuses
  connections, `state.json` records `stopping_at` and `stopped_at`, and a
  `SIGSTOP`ped leader forces escalation.
- [x] 5.2 Implement `logs`: merged `[name]`-prefixed tail, `-f`, a positional
  process filter, `--no-prefix`, and non-fatal missing or empty files. Follow
  mode shall poll liveness from state (excluding zombies) and interleave a
  marker naming the process and pid for an unexpected exit, or a stopped marker
  when `stopping_at` is set. Verify follow mode prints a line appended after it
  starts, a `kill -9` of a node marks it as an unexpected exit, `down` while
  following marks every process stopped, markers never appear in the log files,
  the filter prints one process, and logs remain readable after `down`.
- [x] 5.3 Implement `clean` and verify it removes `.devcluster/<name>/`, leaves
  `config/examples/<name>/` unchanged, and succeeds when nothing exists.
- [x] 5.4 Extend `docs/dev-cluster.md` with stop, logs, cleanup, and the
  follow-mode exit markers (unexpected exit vs expected stop, and why no exit
  code is shown). Verify the documented commands run as written.

## 6. Makefile, documentation integration, and end-to-end check

- [x] 6.1 Add `dev_scaffold`, `dev_up`, `dev_down`, `dev_status`, `dev_logs`,
  and `dev_clean` targets with `CLUSTER` and `ARGS` forwarding, with `dev_up`
  building `//src/gateway/exe:gateway` and `//src/nodeagent/exe:nodeagent`
  first. Verify `make dev_up`, `make dev_status`, `make dev_logs ARGS="-n 5"`,
  and `make dev_down` on a clean tree.
- [x] 6.2 Point the manual start instructions in
  `docs/testing-openworkflow.md` at `make dev_up` and link
  `docs/dev-cluster.md`. Verify the documented commands work as written.
- [x] 6.3 End-to-end check: scaffold a second preset on a different port range,
  start both clusters, confirm echo through both gateways, confirm `git status`
  shows only intended changes, then stop both and confirm every port is free and
  `.devcluster/` is absent from git status.

## Workflow follow-up

- Archive the change after implementation and review.
