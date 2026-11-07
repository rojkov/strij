# Design

## Context

See [proposal.md](proposal.md) for motivation.

The existing harness (`test/e2e/support/`) can generate node and gateway YAML, boot real processes,
prove readiness through marker tasks, submit HTTP tasks, and tear the cluster down. It has no notion
of an event plane: `GatewaySpec` and `WriteGatewayConfiguration` in
`test/e2e/support/cluster_spec.{hh,cc}` only emit HTTP, static discovery, schedulers, and logging.

The runtime pieces the scenario needs already exist:

- `NodeEventClient` selects gateway connections with `GatewayConnectionPool::NextEventCapable`,
  which round-robins over live connections in registration order
  (`src/nodeagent/core/node_event_client.cc`, `src/nodeagent/core/gateway_connection_pool.cc`).
- `capability_aware` routing filters candidate nodes by the task's required pools against the pools
  each node declares (`src/gateway/extensions/schedulers/capability_aware/capability_aware_scheduler.cc`).
- `summary_mesh` broadcasts a route summary on every `AddSubscription` and on its refresh timer, and
  targets peers whose summary matches a published event
  (`src/gateway/extensions/event_planes/summary_mesh/summary_mesh_event_plane.cc`).
- `GatewayRelayTransport` sends peer messages through one live node; the node broadcasts or targets
  gateway connections by gateway id (`src/gateway/core/gateway_relay_transport.cc`,
  `src/nodeagent/core/node_relay_service.cc`).

## Goals / Non-Goals

**Goals:**

- Extend the harness declaratively, not with a one-off process launcher: event-plane config belongs
  in `ClusterSpec`, and gateway boot order belongs in `Cluster::Start`.
- Make both workflow placement and the event's gateway path deterministic, so the relay hop is
  exercised on every run rather than only probabilistically.
- Keep the existing readiness invariant (every gateway serves every node) intact.
- Keep the test black-box: only gateway HTTP responses and the harness's own process diagnostics.

**Non-Goals:**

- No production behavior changes, no event-plane changes, no test-only runtime knobs.
- No separate one-gateway baseline in this change. The two-gateway scenario proves the stronger
  path; a one-gateway test can be added later if diagnostics need isolating.
- No fault injection (killing a gateway/node mid-scenario) and no durable/at-least-once assertions.
- No assertion that reads gateway or node logs to prove the path; logs remain failure diagnostics.

## Decisions

### D1: Two-gateway relay as the primary scenario

The test uses two nodes and two gateways, both with the event plane and both connected to both
nodes. The listener runs on node A and the emitter on node B. The subscription is registered on
gateway G1 and the real event is published to gateway G2, which relays it through a live node to G1.

Alternative: one gateway has a plane and the other does not, forcing all node event traffic through
one gateway. Rejected as the primary test because it never exercises peer relay; it is a useful
debugging fallback, not the acceptance scenario.

Alternative: two gateways but accept whichever path happens (local or relayed). Rejected because a
summary-mesh relay regression could then pass unnoticed.

### D2: Pin workflows by resource declaration, not by gateway topology

Both nodes advertise the `push` protocol and both gateways discover both nodes, preserving complete
bipartite routing and the readiness invariant. Placement is chosen by `capability_aware`:

- Node A declares one pool named `listen`; node B declares one pool named `emit`.
- The gateway routes the default task type through `round_robin` (used by readiness) and binds
  `openworkflow` to `capability_aware`.
- The listener is submitted with `x-strij-resources.listen: 1`; the emitter with
  `x-strij-resources.emit: 1`. `ParamsOnlyRequirementsResolver` turns those headers into the task's
  resource map, and a node that does not declare the pool is ineligible.

Pool totals are `16`: the parent `openworkflow` task holds one unit and its `marker.sh` child holds
another, so a total of `1` would refuse the child.

Alternatives considered:

- Protocol pinning (`push`/`probe`): works, but requires a probe scheduler and probe local scheduler,
  adds moving parts, and still needs a way to keep both nodes visible for readiness.
- Per-gateway static discovery of one node each: breaks complete bipartite relay routing.
- Picking the node by alternating `round_robin` submissions: depends on the readiness sequence and is
  not stable across retries.

### D3: Start and prove gateways one at a time

`Cluster::Start` currently spawns every gateway, then runs one readiness barrier for all of them.
Change it to size `observed_`, then for each gateway: allocate its port, write its config, spawn it,
and run the existing marker readiness barrier for that gateway before spawning the next.

Consequences:

- A gateway is fully connected to every node before the next gateway exists, so each node's
  `GatewayConnectionPool` registration order is gateway start order (`[G1, G2]`).
- `ObservedMarkers()` still ends up with every gateway observing every node; the existing lifecycle
  and fan-out tests keep their assertions.
- A failure on gateway `j` prevents later gateways from spawning and is reported with the same
  diagnostics as today.

Alternative: keep concurrent gateway startup and hope accept order matches spawn order. Rejected:
the relay test's gateway selection would become scheduling-dependent.

### D4: Use an unmatched warm-up event to select the publisher's gateway

The listener's `listen` step performs node A's first event operation (subscribe), so it selects G1
(`NextEventCapable` starts at the first registered connection). Node B's first event operation would
also select G1, which would deliver locally and skip the relay.

The emitter document therefore emits twice:

1. A warm-up event with type `com.example.warmup`, which consumes node B's first event operation
   (G1) and is dropped because G1's subscription pattern only matches `com.example.order.created`.
2. The real event with type `com.example.order.created`, which consumes node B's second event
   operation (G2).

With two event-capable connections, two operations return the cursor to its starting position, so
every retry repeats the same assignment: warm-up to G1, real event to G2.

Alternatives considered:

- A test-only "use gateway X" config: rejected, it changes production surface for a test.
- Injecting raw TLV frames into the node: rejected, it bypasses the black-box HTTP boundary.
- An extra listener/subscription on node B to consume its first cursor slot: workable but heavier
  and leaves a second workflow suspended for the whole test.

### D5: Retry the emitter workflow, not just the publish

`summary_mesh` is best-effort: G2 may publish the real event before G1's subscription summary has
reached it. The test re-submits the emitter workflow until the listener's HTTP request completes.
Each retry runs both emits, so the gateway assignment stays G1-warm-up/G2-real. The emitter's own
response is synchronous, so retries do not overlap.

The test retries only on success or `429 CAPACITY_REFUSED` (which means the workflow never ran and
consumed no event operation). Any other emitter status fails immediately, because a partially run
workflow could leave the event cursor in the wrong phase.

Alternatives: a fixed sleep before the first emit (still racy, slower on average), or tailing logs
for subscription readiness (violates the black-box harness contract).

### D6: Workflow documents and assertions

Both documents live in a test-owned temporary directory and are passed as absolute paths in
`x-strij-function`; child steps call the per-node `marker.sh` relative to the node's working
directory. Node placement is enforced by D2 and asserted by the marker values, so a shared artifact
path is sufficient and no `NodeSpec` file API is needed.

Listener:

```yaml
do:
  - identify:
      call: strij
      with:
        type: piped_executable
        parameters:
          function: marker.sh
        requirements:
          listen: 1
  - wait:
      listen:
        to:
          one:
            with:
              type: com.example.order.created
      output:
        as: '${ {consumer: $input.node, event: .[0]} }'
```

Emitter:

```yaml
do:
  - identify:
      call: strij
      with:
        type: piped_executable
        parameters:
          function: marker.sh
        requirements:
          emit: 1
  - warmup:
      emit:
        event:
          with:
            source: https://example.test/e2e
            type: com.example.warmup
            data:
              producer: '${ .node }'
  - publish:
      emit:
        event:
          with:
            source: https://example.test/e2e
            type: com.example.order.created
            data:
              orderId: 7
              producer: '${ .node }'
```

`listen.output.as` can use `$input`, because the interpreter stores the step's transformed input and
binds it when it shapes the raw output (`SequenceFrame::applyCompletedStep` in
`src/nodeagent/extensions/task_handlers/openworkflow/frame.cc`). The raw output is the consumed-event
array, so `.[0]` is the event data.

Expected bodies:

- Listener: `{"consumer":"n0","event":{"orderId":7,"producer":"n1"}}`
- Emitter: `{"node":"n1"}`

The listener HTTP request is issued on a background thread/future with a 10 s timeout; the main
thread submits emitter workflows with a 5 s timeout and a bounded (8 s) retry deadline, checking the
listener future between attempts. A failure always waits for the listener future before stopping the
cluster, so teardown never races a pending request.

### D7: Harness config representation

`GatewaySpec` gains an optional event-plane description, for example:

```cpp
struct EventPlaneSpec {
  std::string extension_name;
  std::string typed_config_type_url;
  std::string typed_config_yaml;
  std::string gateway_id;
};

struct GatewaySpec {
  // ...
  std::optional<EventPlaneSpec> event_plane;
};
```

`WriteGatewayConfiguration` emits it after the scheduler block:

```yaml
event_plane:
  extension:
    name: "summary_mesh"
    typed_config:
      "@type": "type.googleapis.com/strij.extensions.event_planes.summary_mesh.SummaryMeshConfig"
      refresh_interval_ms: 50
      peer_ttl_ms: 1000
  gateway_id: "gateway-a"
```

The test uses a 50 ms refresh so a lost summary is re-broadcast quickly; the retry loop still owns
correctness. Both gateways use distinct ids (`gateway-a`, `gateway-b`) so targeted relay resolution
is unambiguous.

### D8: Where the new coverage lives

- `test/e2e/support/cluster_spec.{hh,cc}`: `EventPlaneSpec`, `GatewaySpec.event_plane`, writer.
- `test/e2e/support/cluster.{hh,cc}`: sequential gateway start/readiness.
- `test/e2e/cross_node_events_test.cc` (new): the scenario.
- `test/e2e/config_generation_test.cc`: an event-plane round-trip scenario; add the
  `summary_mesh` proto dependency to that target.
- `test/e2e/BUILD.bazel`: the new `cross_node_events_test` target.
- `docs/testing-openworkflow.md`: the harness section and the retry/gateway-order rationale.

### Test flow

```text
main thread                                  node A              gateway G1        gateway G2         node B
    |                                           |                    |                 |                 |
    | Cluster::Start -------------------------->| boot, ready        |                 |                 |
    |                                           |                    |<--- spawn -----|                 |
    |                                           |                    |  ready barrier  |                 |
    |                                           |                    |                 |<-- spawn ------|
    |                                           |                    |                 |  ready barrier  |
    |                                           |                    |                 |                 |
    | POST listener (future) ------------------>| workflow            |                 |                 |
    |                                           | identify -> n0     |                 |                 |
    |                                           | subscribe -------->| register,       |                 |
    |                                           |                    | broadcast       |                 |
    |                                           |                    | summary --------|                 |
    | loop: POST emitter ---------------------->|                    |                 | workflow        |
    |                                           |                    |                 | identify -> n1  |
    |                                           |                    |<-- warmup ------| (G1, dropped)   |
    |                                           |                    |                 | real event ---->|
    |                                           |                    |<-- relay -------| (via node A/B)  |
    |                                           |<-- delivery ------|                 |                 |
    | listener future completes <---------------|                    |                 |                 |
    | assert bodies, then Cluster::Stop ------->|                    |                 |                 |
```

## Risks / Trade-offs

- **Connection-order reliance** -> `Cluster::Start` proves G1 ready before G2 exists, and the test
  documents that it depends on `NextEventCapable` registration order. If the runtime ever changes
  that selection policy, the test must be revisited; the one-gateway fallback still covers
  cross-node delivery.
- **Summary propagation race** -> retries plus a 50 ms refresh interval; a retry never changes which
  gateway the real event uses because it performs two event operations.
- **Retry volume** -> emitter retries only on 200/429, with a 20-50 ms sleep and an 8 s deadline;
  the expected number of retries is small, and the total test stays well inside the short timeout.
- **Pool sizing** -> each pinned pool needs at least two units (parent plus child); the test uses 16.
- **Partial emitter runs** -> a non-200/429 emitter response fails immediately instead of retrying,
  because a workflow that consumed only the warm-up would invert the gateway assignment.
- **Best-effort semantics** -> the test proves delivery on a healthy cluster, not durability or
  at-least-once behavior; those remain out of scope and are covered by their own specs.
- **Parallel repetitions and the bind-close port race** -> `--runs_per_test` with parallel test jobs
  can hand the same ephemeral port to two clusters during teardown, which is a pre-existing harness
  limitation, not an event-path flake. Repeated verification runs use `--local_test_jobs=1`, and the
  testing guide documents that constraint.

## Migration Plan

Test-only change. No deployment or rollback steps; reverting the change removes the new target and
harness fields. Existing e2e tests are unaffected because the default specs carry no event plane and
still start every gateway before the final readiness state is observed.

## Open Questions

None. The optional one-gateway baseline is deliberately deferred to a follow-up if diagnostics need
it; the specifications require only the two-gateway scenario.
