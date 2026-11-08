# Design

## Context

See `proposal.md` — Why. The constraints that shape this design:

- The e2e harness (`test/e2e/support/`) already encodes the required boot and
  teardown discipline: nodes accept before gateways dial, gateways start one at
  a time, readiness is proven through the HTTP path, processes are spawned as
  process-group leaders, logs go to files, and teardown escalates SIGTERM to
  SIGKILL. It is test-scoped by construction: ephemeral ports, a temporary
  directory, in-process ownership, and no persistence after the test binary
  exits.
- A gateway that fails its first node connection never re-dials that address
  (spec `cluster-e2e-harness`), so preset boot order is not cosmetic.
- Listener ports are validated to `1..65535`; port 0 is rejected and
  `SO_REUSEPORT` is reserved for later. Preset ports must therefore be explicit
  and non-overlapping.
- The runtime logs as one text line per record (`severity HH:MM:SS.us tid
  file:line message`); `format: json` and `include_source_location` are
  accepted by config but not implemented. The logger writes to stdout, and
  `log_frontend.cc` currently `printf`s a debug line on every batch.
- Probe scheduling is two-sided: a gateway `probe` scheduler samples nodes that
  advertise `probe` (`RequiredProtocol()`), and a node `probe` scheduler owns
  `kTaskProbe`/`kTaskProbeCancel`/`kTaskGrant`. `capability_aware` and
  `round_robin` instead require `push`, and the node `push` scheduler owns
  `kTaskSubmission`. A node needs `default` as its local authority either way,
  because the `push`/`probe` `Schedule()` paths are deliberately unimplemented.
- `NodeDirectory::GetCandidates(protocol)` filters on availability and protocol
  advertisement, not on handler capability.

## Goals / Non-Goals

**Goals:**

- A persistent local cluster that outlives the command that started it, with a
  predictable one-directory-per-cluster runtime footprint.
- Committed, reviewable presets; generation only as an explicit one-time
  scaffold step.
- Diagnosability: per-process logs, merged viewing, process identity, and
  failure output that doesn't require a rerun.

**Non-Goals:**

- Multi-host orchestration, containers, or anything replacing the e2e harness.
- Log rotation, aggregation, or structured log output.
- Per-process restart or reconnection. A crashed node is recovered by
  `down` + `up`, because the runtime has no re-dial yet.
- Foreground mode with an attached console, port shifting at runtime, and
  dynamic port allocation.
- Recording process exit codes. Only a parent can reap a child, and the cluster
  outlives the command that started it; exact statuses would need a supervisor
  and are deferred.
- Changing runtime protocols, config schemas, or scheduling behavior.

## Decisions

### D1: A stdlib-only Python tool, invoked through Makefile wrappers

`tools/dev_cluster.py` implements `scaffold`, `up`, `down`, `status`, `logs`,
`list`, and `clean`; the Makefile provides thin `dev_*` targets and `dev_up`
builds the two binaries first. Python fits the parsing, process-group, and
log-following work, and the repository already uses Python 3 for tooling.

Alternatives: bash (fragile YAML rendering and prefixed tail multiplexing);
a C++ tool reusing `test/e2e/support` (would force promoting test code into
`src/` or a production target depending on `test/`, for ~80 lines of spawn
logic); Docker Compose (adds an image and io_uring privilege requirements for a
single-machine developer workflow).

### D2: Presets live in `config/examples/<name>/`; ports live in the configs

A preset is a committed directory:

```
config/examples/local/
  cluster.yaml        # description, readiness, process list (name, role, config)
  node-0.yaml         # ordinary NodeAgentConfig YAML
  node-1.yaml
  gateway-0.yaml      # ordinary GatewayConfig YAML
```

`cluster.yaml` deliberately carries no ports: the launcher reads
`tlv_listener.port` or `http_listener.port` from the referenced config, so an
edited config cannot disagree with the manifest. The manifest exists to fix the
process set and boot order and to keep unrelated YAML in the directory from
being launched.

The tool reads the two listener blocks with a small indentation-aware scanner
(not a YAML library) and fails with the expected block shape when it cannot find
a port. Alternatives considered: ports in the manifest (duplication and silent
drift); flat `config/examples/*.yaml` (no topology grouping); runtime-generated
configs in a temporary directory (nothing to review or commit).

### D3: Scaffold once, then edit and commit

`scaffold` is a generator, not part of `up`. It refuses to overwrite unless
`--force` is given, and the committed `config/examples/local/` preset is itself
produced by it, so the generator's output is exercised by the default workflow.
Alternatives: generate on every `up` (git diff noise in a tracked directory and
no stable artifact to review); no generator (every new topology hand-written,
including the every-gateway-knows-every-node wiring).

### D4: Nodes-first boot, request-path readiness

```
 up            node-0..N-1          gateway-0..M-1           .devcluster/
  |                 |                     |                      |
  | read preset     |                     |                      |
  | check all ports |                     |                      |
  | spawn node-0 -->|                     |                      |
  | state.json -----+---------------------+--------------------->|
  | wait :9090 <----|                     |                      |
  | ... all nodes listening ...            |                      |
  | spawn gateway-0 --------------------->|                      |
  | wait :8081 <--------------------------|                      |
  | POST /tasks/echo -------------------->|                      |
  | 200 echo <----------------------------|                      |
  | spawn gateway-1 ... (only now)        |                      |
  | summary + curl hint                   |                      |
```

Readiness is the manifest's `readiness.task_type` submitted through the
gateway's HTTP listener (default `echo`, expected to return the request body);
an empty task type means port-only readiness. This proves routing to at least
one node, not merely a bound socket. Any early process exit aborts the boot,
tails that process's log, stops the processes already started, and exits
non-zero.

### D5: State file per cluster, retained after stop

`.devcluster/<name>/state.json` is written after each spawn, so an interrupted
`up` leaves enough information for `down` to clean up. It records name, role,
pid, process group, port, config path, log path, and start time, and is
rewritten with `stopped_at` on `down` instead of being deleted. Retaining it
keeps `status` and `logs` meaningful after a crash and preserves the pid mapping
for core dumps; `clean` is the explicit removal. `down` also records
`stopping_at` before it signals anything, so a concurrent log viewer can tell
an expected stop from an unexpected exit. A `flock` on
`.devcluster/<name>/.lock` serializes `up`/`down` for the same name.

`up` treats an existing state as live only if a listed pid is alive; stale state
is replaced. Live state blocks a second `up`.

### D6: Teardown through process groups

```
 down        gateway groups        node groups
  |               |                    |
  | SIGTERM ----->|                    |
  | wait grace    |                    |
  | SIGTERM -------------------------->|
  | wait grace    |                    |
  | SIGKILL stragglers --------------->|
  | verify ports refuse connections    |
  | state.json marked stopped          |
  | logs kept                          |
```

Each process is spawned with `start_new_session=True`, so it is a session and
process-group leader; task descendants (e.g. `piped_executable` children)
inherit the group unless they call `setsid`. Gateways are signalled first to
avoid node-disconnect churn, then nodes. Grace defaults to 5 s, then the group
is force-killed. Teardown fails if a former listener port still accepts.

### D7: Merged log view keyed by process name

`logs` reads state, tails each file (default 100 lines), and writes
`[<name>] <line>`; `-f` follows via one reader thread per file feeding a queue;
a positional argument filters to one process and `--no-prefix` prints raw
lines. The prefix is the process name only — **pids are reported by `status`**,
which is the single place to find the debugger attach target. Missing or empty
logs are reported and do not fail the command.

Regular files cannot be polled with `select`, so follow mode uses blocking
reader threads rather than a selector. The viewer does not parse log fields.

Follow mode also runs a liveness poll over the state's pids (about every 500 ms)
and interleaves synthetic markers into the merged stream: an exit observed while
`stopping_at` is absent is reported as
`*** exited unexpectedly (pid N); cluster is degraded`, and an exit observed
during a stop as `*** stopped`. Liveness is `kill(pid, 0)` plus a
`/proc/<pid>/stat` state check, so a zombie that has not been reaped yet is not
counted as alive. Markers go to the viewer only; the log files are never
modified. No exit status is reported: the processes are orphans of the exited
`up`, so only a parent can reap the reason, and the supervisor that would fix
that is a non-goal here. An exit that happens with no viewer attached is still
visible afterwards through `status`, which reports the process as stopped.

### D8: Probe by default, with the node side generated to match

Generated gateways bind `probe` (`candidate_count` 2,
`probe_deadline_ms` 1000). Generated nodes declare three schedulers:

- `probe` — advertises `probe`, owns `kTaskProbe`/`kTaskProbeCancel`/`kTaskGrant`;
- `push` — advertises `push`, owns `kTaskSubmission`; unused by default but lets
  a preset switch the gateway to `capability_aware`/`round_robin` by editing
  only the gateway file (the frame types are disjoint);
- `default` — `local_default: true`, the local authority for child tasks.

The scaffold invariant is a homogeneous handler set on every node. This matters
because probe candidate selection filters only on protocol advertisement, so a
node without a task's handler can still pull the probe and fail at grant with
`kTaskRejected`. Heterogeneous presets would need `capability_aware` (which
filters on handler capability) until probe learns the same filter. `--scheduler`
exposes that choice at scaffold time.

### D9: Debug logging by default

Scaffold writes `logging.level: debug`, `format: text`, `output: stdout`. Debug
is what makes a local investigation useful, but it also amplifies the stray
`printf` in `log_frontend.cc`, which prints
`LogFrontend::HandleCompletion called with res=...` on every batch. Removing
that line is the first task; the viewer's tail limit and prefix keep the volume
manageable. The design does not advertise `format: json` or
`include_source_location`, which are accepted by config but not implemented.

### D10: Fixed ports, distinct per preset

Presets own fixed ranges (`local` uses gateway 8081 and nodes 9090/9091).
Scaffold rejects a range used by another preset and suggests an alternative;
`up` binds-checks every port before spawning. Fixed ports keep the committed
YAML honest and the curl commands stable. Alternatives: ephemeral allocation
like the e2e harness (ports change every run, unusable in committed presets);
runtime shifting via CLI overrides (silently disagrees with the committed file).

### D11: Repo root as working directory

Every process starts with the repository root as cwd and an absolute
`--config_file`. Relative paths inside tasks (for example an
`x-strij-function` executable) then resolve predictably, independent of where
the preset lives.

### D12: The tool does not invoke Bazel

`make dev_up` builds `//src/gateway/exe:gateway` and
`//src/nodeagent/exe:nodeagent` and then runs the tool. Direct tool invocation
uses `bazel-bin/...` and fails with a build hint when a binary is absent.

## Risks / Trade-offs

- **No re-dial means a crashed node is not recovered** → boot barrier catches
  startup failures; a mid-life crash is recovered with `down` + `up`. Per-node
  restart is explicitly out of scope until reconnection lands.
- **Probe can misroute to a node lacking the handler** → scaffold keeps handler
  sets homogeneous and documents `capability_aware` as the escape hatch.
- **Debug logs grow quickly** → truncate on `up`, default tail of 100, `clean`
  for removal; no rotation in v1.
- **Stale state and pid reuse** → liveness check on every `up`, retained state
  is advisory for `status`/`logs`, `clean` resets.
- **Exit markers need an attached viewer** → they are best-effort while `-f`
  runs; afterwards `status` still shows the process stopped. Exact exit codes
  need a supervisor and are deferred.
- **Two presets started simultaneously collide** → distinct preset ranges plus
  a runtime bind-check with a clear error.
- **Duplication with the e2e harness** → the tool reimplements only spawn,
  port-wait, and teardown; the harness remains the behavioral authority and
  none of its code moves.
- **`summary_mesh` is best-effort** → the dev cluster may drop events published
  before a subscription summary propagates; it is for development, and the e2e
  scenario keeps its bounded retry.

## Migration Plan

No runtime migration. Land the tool, add `.devcluster/` to `.gitignore`,
generate and commit `config/examples/local/`, and point the manual start
instructions in `docs/testing-openworkflow.md` at `make dev_up`. Rollback is
deleting the tool and preset plus reverting the two one-line changes (Makefile
targets, ignored path); nothing in the runtime depends on them.

## Open Questions

None blocking. Foreground mode, per-process restart, and log rotation are
recorded as non-goals rather than deferred questions.
