# Proposal

## Why

The repository has no automated test that starts the real `gateway` and `nodeagent` binaries. The
closest coverage — the two-gateway, relay-node, two-node event-plane test — hand-wires fake
mailboxes, a fake relay, and a mock dispatcher inside one process, so it cannot observe YAML loading,
extension registration, `io_uring`, real TCP framing, connection handshakes, or shutdown. The only
real-process check is the manual `curl` procedure in `docs/testing-openworkflow.md`.

A first manual multi-process attempt (2 nodes x 2 gateways) immediately exposed behavior that no
in-process test could show: a gateway whose first connect attempt loses a startup race never
re-dials that node and silently reports `429`, and a node that receives `SIGTERM` mid-task leaves the
task's grandchildren running. Neither is covered today because nothing boots a cluster.

The `openworkflow-emit-listen` change just landed, which makes the gap urgent: its acceptance
scenario ("emit on node A, listen on node B through two gateways") is currently only proven against
simulated peers.

## What Changes

- Add a C++ GoogleTest cluster harness under `test/e2e/support/` that can start N `nodeagent`
  processes and M `gateway` processes from generated YAML, wait for a defined readiness condition,
  issue HTTP task submissions, and tear the cluster down deterministically.
- Add end-to-end tests for the fan-out baseline: boot topology, concurrent task fan-out through
  every gateway, per-node pool admission, and clean shutdown.
- Encode the two-phase boot invariant in the harness: nodes must be listening before gateways dial,
  because a failed gateway connect is never retried.
- Add a `strij_e2e_test` Bazel macro carrying `data` (the two binaries), `timeout`, and
  `tags = ["requires-network"]`, since `strij_cc_test` accepts none of these.
- Add a `make test_e2e` target, and include `//test/e2e` in the default test graph.
- Skip the tests with a clear message when `io_uring_setup` is unavailable in the sandbox, instead
  of failing for an environment reason the change cannot fix.
- Record, without fixing, three defects the harness surfaced: gateways never reconnect; 
  `utils::GetErrorString()` returns a dangling pointer (mojibake in the connect-failure log); a
  node's task grandchildren survive node shutdown.

## Capabilities

### New Capabilities

- `cluster-e2e-harness`: automated, per-test-isolated clusters of real gateway and nodeagent
  processes — generated configuration, defined readiness, black-box request assertions,
  deterministic teardown, and environment-aware skipping.

### Modified Capabilities

None. No runtime requirement in an existing spec changes; the harness observes current behavior.

## Impact

- **New code:** `test/e2e/` (harness library plus four test targets). No production code changes.
- **Build:** new `strij_e2e_test` macro in `bazel/build_system.bzl`; new `BUILD.bazel` packages under
  `test/e2e/`; the gateway and nodeagent binaries become test `data` dependencies.
- **Developer workflow:** `make test_e2e`; `make test` now also runs `//test/e2e`.
- **Docs:** `docs/testing-openworkflow.md` gains an automated-harness section; the manual procedure
  stays as the fallback for hosts without `io_uring`.
- **Out of scope:** gateway reconnect/backoff, the dangling `GetErrorString()`, orphaned task
  grandchildren, and emit/listen across real processes. The harness is shaped so the last one can be
  added later without reworking the support library.
