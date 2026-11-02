# Design

## Context

See `proposal.md` for motivation. This section records only the constraints that shape the harness.

Test tree today: unit and in-process integration tests only, built with `strij_cc_test`, which
accepts `srcs`, `deps`, and `data` but no `timeout` or `tags`. Nothing in `test/` spawns a process
(`posix_spawn`/`fork`/`execv` appear nowhere under `test/`). The closest multi-gateway coverage
(`test/gateway/extensions/event_planes/summary_mesh/summary_mesh_event_plane_test.cc`) hand-wires
mailboxes, a relay, and a mock dispatcher in one process.

Measured on this host with the already-built binaries:

| Observation | Value |
|---|---|
| Node TLV listener accepting | 16 ms after spawn |
| 2 nodes x 2 gateways serving from every node | 49-91 ms |
| 40 concurrent `echo` tasks through 2 gateways | 40/40, distinct bodies |
| `SIGTERM` -> exit, idle cluster | 1-8 ms, status 0 |
| Pool `total: 1` per node, 4 concurrent requests | 2 x 200, 2 x 429, one 200 per node |

Constraints the binaries impose:

- **No reconnect.** `Node::HandleCompletion` sets `kDisconnected` on a failed connect and never
  retries; `StaticNodeDiscovery::Start` delivers its snapshot once (`static_node_discovery.cc:18`).
  A gateway started before a node is listening reports `429` for that node forever.
- **No readiness signal.** No process logs that it is listening. The gateway logs
  `Node {} advertised capabilities` per connected node, but only at `level: info`.
- **No port reporting.** `TcpListener` binds `INADDR_ANY`, ignores the configured address, and
  exposes no bound port, so `port: 0` is unusable as a caller. It also sets `SO_REUSEPORT`, so an
  accidental collision silently shares the port instead of failing.
- **No response streaming contract beyond HTTP.** Final results use `Content-Length` plus
  `Connection: close`; errors do the same, so a client can read to EOF.
- **Descendants are not reaped.** A node receiving `SIGTERM` mid-task kills the immediate
  `piped_executable` child but not that child's children; two orphaned processes were observed after
  a clean exit with status 0.
- **`io_uring_setup` is mandatory.** Both binaries construct `DispatcherImpl` before any validation
  short-circuit, so a host that denies the setup call crashes them at startup.

## Goals / Non-Goals

**Goals:**

- A support library good enough that a new cluster scenario is a data specification plus HTTP
  assertions.
- Readiness and teardown that are deterministic under repetition and under parallel test binaries.
- Failures that are diagnosable from gtest output alone, without rerunning by hand.

**Non-Goals:**

- No fault injection in this change (no killing nodes or gateways mid-test), no emit/listen scenario,
  no CI matrix, no performance measurement.
- No change to gateway or nodeagent behavior, even where the harness exposes a defect.
- Not a general-purpose process supervisor: no restart, no supervision, no log streaming to files
  beyond a per-process capture.

## Decisions

### D1: Real subprocesses, not in-process frameworks

Each test spawns the built `gateway` and `nodeagent` binaries. Alternatives considered:

- Multiple `RunGateway()`/`RunNodeagent()` calls on threads. Rejected: both parse global `absl`
  flags, install per-thread `signalfd` handlers, and start a process-global logger singleton.
- Extending the existing in-process integration test. Rejected: it cannot observe config loading,
  factory registration, socket framing, or shutdown, which is the entire point.

Subprocesses also let the harness assert the property that only a real process has: exit status.

### D2: C++ GoogleTest support library under `test/e2e/`

The harness is C++, built with gtest, in `test/e2e/support/`. Alternatives:

- Python driver. Lower line count, but `rules_python` is declared without a registered
  `python.toolchain`, and it introduces a second language into a C++-only tree.
- Shell plus `curl`. Rejected: bounded retries, barriers, concurrency, and exit-code assertions in
  bash are where flaky and unreadable tests come from.

The library is split so a future emit/listen scenario reuses everything: `cluster` (spec ->
processes), `process` (spawn/terminate/reap), `http_client` (submission), `diagnostics` (log tails).

### D3: Two-phase boot, enforced by API

`Cluster::Start` never returns until `SpawnGateways()` has run, and `cluster.Start` is the only
supported entry point. Nodes are spawned and individually confirmed listening first:

```
  Cluster::Start(spec)
    |
    +-[1] allocate a port per node, per gateway, one at a time, immediately before spawn
    |
    +-[2] spawn node[i]  cwd=<tmp>/node[i]/   stdout+stderr -> <tmp>/node[i].log
    |
    +-[3] for each node: poll TCP connect until accept or process exit
    |        exit  -> FAIL with exit status + log tail   (spec: boot failure)
    |        timeout -> FAIL with log tail
    |
    +-[4] spawn gateway[j]  stdout+stderr -> <tmp>/gateway[j].log
    |
    +-[5] readiness barrier (D4)
```

Alternative: a fixed sleep. Rejected — it is both slow and flaky, and it hides the invariant instead
of asserting it. The barrier costs 16 ms and turns a silent `429` into a named failure.

A TCP connect is safe as a liveness probe: the node registers the accepted mailbox and the pool
marks it dead and prunes it when the probe closes (`gateway_connection_pool.cc:16`).

### D4: Readiness is asserted through the data path

After gateways spawn, the barrier submits a marker task through **each** gateway until every node
marker has been observed at that gateway, bounded by a deadline. This is the same assertion the
fan-out test needs, so readiness costs nothing extra and cannot drift from the property under test.

```
  for each gateway g:                       bounded by 5 s
    while observed[g] != {n0..nk}:
      submit marker task to g
      if 200: observed[g] += body.node          (round_robin rotates nodes)
      else if 429: retry after 5-10 ms          (node not attached yet)
```

Alternatives:

- Scrape `advertised capabilities` from gateway logs. Rejected: it requires `level: info`, it couples
  the suite to log text, and it proves the *advertisement* arrived, not that a task can be routed.
- Add a status endpoint. Rejected for this change: production surface for test convenience, and it
  cannot prove the data path anyway.

### D5: Node identity comes from a per-node working directory

`LocalFunctionResolver` returns the function reference verbatim, and `piped_executable` runs it
relative to the node's cwd. So each node gets its own directory containing `marker.sh`, and every
gateway returns the marker of the node that served the request:

```
  <tmp>/node[0]/marker.sh -> {"node":"n0"}
  <tmp>/node[1]/marker.sh -> {"node":"n1"}
  request: POST /tasks/piped_executable  x-strij-function: marker.sh
```

Verified: alternating `{"node":"n0"}`, `{"node":"n1"}` through both gateways. Alternatives:
distinct handler configuration per node (verbose, no identity), or a production field in the result
(rejected: changes the wire protocol for tests).

### D6: Ports from the operating system, allocated just-in-time

`bind(0)` / `getsockname` / `close` per process, immediately before that process is spawned, rather
than a fixed base port or one batch up front. This keeps the vulnerable window small and removes the
need to reason about shard or worker identity. Residual risk is covered in Risks.

### D7: Teardown escalates through the process group

Each child is spawned as its own process-group leader. Teardown sends the termination signal to the
group, waits a bounded interval, then sends a forced kill to the group, and reaps every process.

```
  stop(all):
    for p: killpg(p.pgid, SIGTERM)
    wait up to 2 s for all to exit          (poll)
    for p still alive: killpg(p.pgid, SIGKILL); mark escalated
    reap every pid; record exit status
    assert: status == 0 for every process (unless escalated)
    assert: no descendant of any group remains
    assert: connecting to each former listener is refused
```

Sending to the group rather than the pid is what removes task grandchildren; this is a harness
property, not a fix for the node's leak.

### D8: Logs go to files, never to pipes

Children write to per-process files under `$TEST_TMPDIR`. Pipes were rejected: a node at debug level
can fill a 64 KiB pipe buffer and block on write, which would deadlock the harness while it waits
for readiness. Files also supply D9 for free.

### D9: Diagnostics are collected structurally

`Cluster` keeps the spec, the port map, and one log path per process. `Cluster::Start` returns
`absl::Status` rather than failing inside the constructor, and a non-ok status carries the process
name, exit status, and log tail. That keeps the happy path one `ASSERT_OK` and makes the failure
path itself testable: a test can start a deliberately misconfigured cluster and assert on the
message. Every other assertion helper that can fail on a cluster condition appends a `Diagnose()`
block with process status and the last N lines of each log to the gtest failure message.

### D10: Skip on a setup probe, not on an error path

Each test calls `RequireIoUring()` before `Cluster::Start`, which issues the setup syscall and
`GTEST_SKIP()`s with a message when it fails. Skipping is opt-out only for this one environmental
condition; a crashed child is always a failure.

### D11: New `strij_e2e_test` macro rather than extending `strij_cc_test`

`strij_cc_test` has a narrow signature and is used everywhere. A sibling macro in
`bazel/build_system.bzl` sets `data = [binaries]`, `tags = ["requires-network"]`, `timeout =
"short"`, and `size`, keeping the change additive.

Binaries are data dependencies resolved through runfiles (`TEST_SRCDIR`/`TEST_WORKSPACE`), not
through `bazel-bin` paths, so the tests work under the sandbox and in remote execution.

### D12: Assert coverage and counts, never scheduling order

A fan-out test asserts that every gateway observed every node, that every body came back once, and
that per-node capacity produced the expected number of `429`s. It does not assert the exact
round-robin sequence: that would pin scheduler internals and break on legitimate scheduler changes.

## Sequence Diagrams

### Cluster boot and readiness

```
  test              harness                 node[i]              gateway[j]
   |                  |                        |                     |
   | Start(spec) ---->|                        |                     |
   |                  | bind(0) per node       |                     |
   |                  | spawn (pg leader) ---->| listen              |
   |                  | poll connect --------->| accept (probe)      |
   |                  |<------- ok ------------|                     |
   |                  | spawn gateway[j] --------------------------->|
   |                  |                        |<---- dial + hello ---|
   |                  |                        |---- advertisement -->|
   |                  | POST marker (gw j) -------------------------->|
   |                  |<---------------- 200 {"node":"n0"} -----------|
   |                  | POST marker (gw j) -------------------------->|
   |                  |<---------------- 200 {"node":"n1"} -----------|
   |                  | observed[j] = {n0,n1} -> ready              |
   |<---- ready ------|                        |                     |
```

### Teardown with escalation

```
  test              harness                 node[i]         grandchild
   |                  |                        |                |
   | Stop() --------->| killpg(SIGTERM) ------>| exit 0         |
   |                  |                        |  (child killed, |
   |                  |                        |   grandchild   |
   |                  | wait <= 2 s            |   survives) ---|-- alive
   |                  | (if alive) killpg(SIGKILL) ------------->|-- gone
   |                  | waitpid all            |                |
   |<---- ok ---------| assert no descendant   |                |
```

## Risks / Trade-offs

- **Bind-close port race** -> a port handed out by the OS may be taken by another process before the
  child binds it, producing a confusing startup failure. Mitigated by allocating immediately before
  spawn, by failing fast with the child's log, and by keeping clusters per-test rather than
  long-lived. `SO_REUSEPORT` makes an accidental double bind silent rather than an error; the
  readiness barrier converts that into a test failure instead of an intermittent wrong answer.
- **io_uring capability beyond setup** -> the probe only proves the setup call; a ring feature may
  still fail. Mitigation: a crash is a hard failure with the child log, and the manual procedure in
  the docs remains the fallback.
- **Sandbox networking** -> the tests bind loopback listeners. Mitigation: the
  `requires-network` tag documents the requirement; no external address is ever used.
- **Test binary killed by the runner** -> the harness teardown never runs, so children may outlive
  it. Mitigation: children are process-group leaders, so a runner that kills the test's process
  group takes them too; the risk that remains is a runner that kills only the test pid.
- **Parallel load skew** -> many e2e clusters on a busy machine stretch deadlines. Mitigation:
  deadlines are generous (5 s readiness), assertions are counts not latencies, and clusters cost
  about 100 ms so tests need not be sharded.
- **Log volume** -> debug-level logs for a long-lived cluster can grow large. Mitigation: e2e
  configurations use `level: warn` except where a test needs more, and logs live in
  `$TEST_TMPDIR`.

## Migration Plan

Purely additive. Land the macro, the support library, and the tests; `make test` picks up
`//test/e2e` automatically. Rollback is deleting the new targets; no production code, config, or
wire format is touched. `docs/testing-openworkflow.md` gains an automated section while keeping the
manual procedure as the no-`io_uring` fallback.

## Open Questions

- Whether the suite should also run under the sanitizer configurations, which roughly triple startup
  cost and need larger readiness deadlines. Deferred; the deadlines are adjustable when that is
  decided.
