# Tasks

## 1. Build plumbing and environment guard

- [x] 1.1 Add a `strij_e2e_test` macro to `bazel/build_system.bzl` that sets `data`, `tags =
      ["requires-network"]`, `timeout = "short"`, and `size`, leaving `strij_cc_test` untouched.
      Verify with `bazel query 'kind("cc_test", //test/e2e/...)'` after 1.3 that the target is
      declared and carries the tag.
- [x] 1.2 Add `test/e2e/support/environment.{hh,cc}` with `RequireIoUring()`, which issues the
      event-loop setup syscall and `GTEST_SKIP()`s with a message naming the missing facility
      otherwise. Verify on this host that it passes, and verify by inspection that the skip branch
      returns before any process is spawned.
- [x] 1.3 Add `test/e2e/BUILD.bazel` with an `environment_guard_test` target built from the new
      macro. Verify with `bazel test //test/e2e:environment_guard_test`.

## 2. Process supervisor

- [x] 2.1 Add `test/e2e/support/process.{hh,cc}` with `Spawn` that starts a process as its own
      process-group leader, sets its working directory, redirects stdout and stderr to a per-process
      log file, and returns its pid, process-group id, and log path. Verify with a unit test that
      spawns `/bin/sh -c 'echo hi'` and observes exit status 0 and `hi` in the log.
- [x] 2.2 Add `WaitForPort` (bounded TCP connect poll), `HasExited`, `ExitStatus`, and
      `LogTail(path, lines)` to the same library. Verify with a unit test that polls a port opened by
      a spawned process and reports a timeout for a port nobody listens on.
- [x] 2.3 Add `Terminate` that signals the process group, waits a bounded interval, escalates to a
      forced kill of the group, reaps every pid, and reports whether escalation occurred. Verify
      with a unit test whose child traps and ignores the termination signal, asserting escalation
      happened and the process is reaped.
- [x] 2.4 Add `DescendantsRemaining(process)` and verify it reports a child that a spawned shell
      leaves running, and reports none after `Terminate`.

## 3. Cluster specification and configuration generation

- [x] 3.1 Add `test/e2e/support/cluster_spec.{hh,cc}` with node and gateway specifications covering
      listener port, resource pools, task handlers, local schedulers, gateway schedulers, and a node
      marker name, plus defaults for the fan-out topology. Verify with a unit test that the default
      spec produces the expected node and gateway counts.
- [x] 3.2 Add configuration generation that writes one directory per process containing its YAML
      configuration and a `marker.sh` and returns the paths. Verify with `config_generation_test`
      that a generated gateway YAML round-trips through the real `LoadConfig<GatewayConfig>` and a
      generated node YAML through `LoadConfig<NodeAgentConfig>`, with every node address present and
      the ports and pools preserved.
- [x] 3.3 Verify with the same test that a specification missing a resource pool is still generated
      as written (the harness does not silently repair invalid input), so the resulting startup
      failure is reported by the cluster, not hidden by the generator.

## 4. Cluster lifecycle

- [x] 4.1 Add a minimal HTTP client (`Post(port, path, body, headers)`) that sends a request, reads
      the response to EOF, and returns status code and body, with a per-request deadline. Verify it
      against the first live cluster in 4.5.
- [x] 4.2 Implement `Cluster::Start`: allocate a port per process immediately before spawning it,
      spawn nodes, wait for each node listener to accept (failing fast with the child log when it
      exits), spawn gateways, then run the marker-based readiness barrier and return
      `absl::Status`. Verify with `cluster_lifecycle_test` that a 2x2 cluster reaches the state where
      every gateway has observed every node marker.
- [x] 4.3 Implement `Cluster::Stop`: signal each process group, wait a bounded interval, escalate,
      reap, assert exit status, and check that no descendant survives and that each former listener
      port refuses connections. Verify with a test that asserts all of these on a stopped 2x2
      cluster.
- [x] 4.4 Wire diagnostics into every failure path so that a non-ok `Start` and every failed
      cluster assertion include the process name, status, and log tail. Verify with a test that
      starts a cluster whose node configuration omits a resource pool and asserts the returned status
      message contains the node's startup error text.
- [x] 4.5 Add `cluster_lifecycle_test` covering readiness, clean shutdown, escalation, and the
      invalid-configuration failure path; verify with `bazel test //test/e2e:cluster_lifecycle_test`.

## 5. Fan-out behavior tests

- [x] 5.1 Add `task_fanout_test` asserting that every gateway serves at least one task from every
      node in a 2-node, 2-gateway cluster, sourced from the marker results. Verify with
      `bazel test //test/e2e:task_fanout_test`.
- [x] 5.2 Add a concurrency case submitting 40 distinct bodies concurrently through both gateways
      and asserting every request succeeds and every response body matches its own request. Verify
      that the test passes repeatedly (at least 20 consecutive runs) without a flake.
- [x] 5.3 Add a per-node capacity case with one unit of shared pool per node and four concurrent
      requests requiring one unit, asserting exactly two successes and two `CAPACITY_REFUSED`
      responses, with one success attributable to each node marker. Verify with
      `bazel test //test/e2e:task_fanout_test --test_filter=*Capacity*`.

## 6. Developer workflow and documentation

- [x] 6.1 Add a `test_e2e` Makefile target that runs `//test/e2e/...` and confirm the default test
      target already includes it. Verify by running both targets and observing that the end-to-end
      tests execute in each.
- [x] 6.2 Document the automated harness in `docs/testing-openworkflow.md`: what it proves, how to
      run it, the two-phase boot invariant, the skip condition, and that the manual procedure remains
      the fallback. Verify the documented commands run as written.
- [x] 6.3 Verify the skip path end to end by running a target in an environment where the event-loop
      setup call is denied (or a temporary probe override) and confirming the test reports skipped
      with the explanatory message rather than failed.

## Workflow follow-up

- Archive the change after review.
- Open a separate change for the three out-of-scope defects observed while designing this suite:
  gateway reconnect/backoff, the dangling `utils::GetErrorString()` return, and task grandchildren
  surviving node shutdown.
- Add the emit/listen-across-real-processes scenario on top of this harness.
