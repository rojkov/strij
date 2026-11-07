# Proposal

## Why

The emit/listen path is only proven hermetically (a `FakeEventBus` in `emit_test`/`listen_test`) or
by an in-process test that hand-wires mailboxes, a relay node, and a mock dispatcher
(`summary_mesh_event_plane_test`). Neither boots the real `gateway` and `nodeagent` binaries, loads
the event-plane YAML, registers `summary_mesh`, or uses real TCP/TLV framing. The archived
`e2e-cluster-harness` change explicitly left "emit/listen across real processes" as the follow-up,
and its acceptance scenario is a workflow paused on a `listen` in one node being woken by an `emit`
from another node through the gateway event plane.

The harness cannot express that scenario yet: `GatewaySpec` has no event-plane configuration and
the generated gateway YAML never contains an `event_plane` section.

## What Changes

- Extend the cluster specification and YAML generator with an optional gateway `event_plane`
  (extension name, typed config, gateway id) so the real gateways run the bundled `summary_mesh`
  plane.
- Start gateways one at a time and prove each ready before spawning the next, making each node's
  event-capable gateway-connection order deterministic.
- Add a real-process end-to-end test: a workflow on node A suspends in `listen`, and a workflow on
  node B emits a matching CloudEvent; the test asserts the listener's HTTP response identifies the
  consumer node and the event's producer node.
- Pin workflows to their intended nodes without breaking complete-bipartite readiness: give each
  node a unique pool name, bind `openworkflow` to `capability_aware`, and submit each workflow with
  the matching `x-strij-resources.<pool>` requirement.
- Force the event across gateways: the emitter workflow publishes an unmatched warm-up event first
  and the real event second, so the listener's gateway holds the subscription and the real event is
  published to the other gateway and relayed through a node. Retry the emitter workflow until the
  listener completes because v1 delivery is best-effort.
- Optionally add a one-gateway, two-node baseline that proves cross-node delivery without the relay
  hop.
- Document the new scenario in `docs/testing-openworkflow.md`.
- No production code changes; the test observes existing event-plane and relay behavior.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `cluster-e2e-harness`: the harness gains optional event-plane configuration generation,
  sequential gateway boot/readiness, and a cross-node `emit`/`listen` scenario driven only through
  the gateway HTTP listener.

## Impact

- **Test support:** `test/e2e/support/cluster_spec.{hh,cc}` gains the event-plane specification and
  writer; `test/e2e/support/cluster.{hh,cc}` starts and verifies gateways sequentially.
- **Tests:** a new `test/e2e` target (for example `cross_node_events_test`) plus a config-generation
  scenario proving the event-plane YAML round-trips through `LoadConfig<GatewayConfig>`.
- **Build:** `test/e2e/BUILD.bazel` gains the new target and any `summary_mesh` proto dependency used
  by the config-generation assertion.
- **Docs:** `docs/testing-openworkflow.md` describes the real-process event scenario and its retry
  and gateway-order assumptions.
- **Runtime code:** none. Any failure this test finds is a separate fix, not part of this change.
