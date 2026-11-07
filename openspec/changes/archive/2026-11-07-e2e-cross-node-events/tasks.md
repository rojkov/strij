# Tasks

## 1. Event-plane configuration in the cluster spec

- [x] 1.1 Add `EventPlaneSpec` and an optional `GatewaySpec.event_plane`, and emit the generated
  `event_plane` YAML block (extension name, typed config, gateway id) from
  `WriteGatewayConfiguration`. Verify with a new `config_generation_test` scenario that
  `LoadConfig<GatewayConfig>` reports `has_event_plane()`, preserves the gateway id, and unpacks the
  typed config to `SummaryMeshConfig`; add the `summary_mesh_cc_proto` dependency to that target.
- [x] 1.2 Verify the absence case: a gateway spec without an event plane generates no `event_plane`
  section and `LoadConfig<GatewayConfig>` reports `has_event_plane()` false; run
  `bazel test //test/e2e:config_generation_test` and confirm the existing fan-out assertions still
  pass.

## 2. Sequential gateway boot

- [x] 2.1 Refactor `Cluster::Start`/`startGateways`/`waitForReadiness` so each gateway is spawned and
  proven ready (every node marker observed) before the next gateway is spawned, and
  `ObservedMarkers()` still exposes every gateway's set after `Start`. Verify with
  `bazel test //test/e2e:cluster_lifecycle_test //test/e2e:task_fanout_test`.
- [x] 2.2 Document the sequential-boot invariant and its reason (deterministic per-node gateway
  connection order) in the harness class comment and in the automated-harness section of
  `docs/testing-openworkflow.md`; verify the documented description matches the code and the
  existing e2e targets still pass.

## 3. Cross-node event scenario

- [x] 3.1 Add the test-only topology: node A with a `listen` pool, node B with an `emit` pool, both
  with `piped_executable` and `openworkflow` (jq evaluator) handlers; a gateway with default
  `round_robin` plus an `openworkflow` binding to `capability_aware`; and both gateways carrying
  distinct `summary_mesh` event-plane identities. Verify the generated YAML through
  `config_generation_test` or an equivalent assertion that the pools, handlers, scheduler bindings,
  and event-plane ids are present.
- [x] 3.2 Add the listener and emitter workflow documents and implement the scenario: submit the
  listener on a future, retry the emitter workflow (200 or 429 only) until the listener completes or
  a bounded deadline expires, and assert the exact listener and emitter response bodies. Verify with
  `bazel test //test/e2e:cross_node_events_test`.
- [x] 3.3 Make failures diagnosable: a non-200/429 emitter response fails immediately, a listener
  deadline names the retry budget, the listener future is always joined before `Cluster::Stop`, and
  every failure includes `Cluster::Diagnose()`. Verify by a local run with a deliberately mismatched
  event type to observe the timeout message and diagnostics (do not commit the mismatch).
- [x] 3.4 Verify determinism with repeated runs:
  `bazel test //test/e2e:cross_node_events_test --runs_per_test=20`, with no flaky gateway path or
  retry exhaustion.
- [x] 3.5 Document the new scenario in `docs/testing-openworkflow.md`: what it proves, the
  two-gateway relay path, the best-effort retry rationale, and the bounded deadlines; verify the
  documented commands run as written.

## 4. Suite integration

- [x] 4.1 Run the whole end-to-end suite repeatedly to check interaction between the new target and
  the existing clusters: `bazel test //test/e2e/... --runs_per_test=10`.
- [x] 4.2 Run `make test_e2e` and `openspec validate e2e-cross-node-events --strict`; both pass, and
  `bazel query 'tests(//test/e2e/...)'` includes `cross_node_events_test`.
- [x] 4.3 Confirm the new test skips rather than fails where `io_uring` is unavailable, using the same
  `RequireIoUring()` guard as the other e2e tests; verify by inspection and, when available, an
  environment where the probe is denied.

## Workflow follow-up

- Keep the deferred one-gateway cross-node baseline as a follow-up change only if the relay test
  proves hard to diagnose.
- Open a separate change for any runtime defect the new test exposes; this change is test-only.
