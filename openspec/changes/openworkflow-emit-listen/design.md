# Design

## Context

See `proposal.md` for motivation and `specs/` for the behavior contract.

The current runtime is a star: each gateway dials every node it discovers, and each node accepts connections from every gateway. The nodeagent is a passive worker; the gateway owns cluster-scoped routing state (`NodeDirectory`, `ResultReceiverStorage`, scheduler routing). There is no event plane. The Open Workflow parser already produces `EmitBody` and `ListenBody`, but `SequenceFrame::Advance` reports them as unimplemented.

The v1 transport must exist without an external broker. The deployment invariant is that every gateway connects to every node. The event plane must be a gateway extension, the framework must stay thin, and the v2 durable at-least-once backend will be an external clustered backbone first, with a self-hosted replicated log remaining possible behind the same contracts.

## Goals / Non-Goals

**Goals:**

- Implement `emit` and `listen` in the openworkflow interpreter on a real cross-gateway event plane.
- Keep the event plane backend-agnostic so an external-backbone extension and a future self-hosted extension can replace the v1 implementation.
- Keep the framework out of gateway peer discovery and peer connection management.
- Deliver v1 as best-effort, at-most-once, with no external dependency.
- Preserve the seams needed for v2 durable at-least-once: stable subscription identity, delivery tokens and acknowledgements, detach versus delete, duplicate tolerance, and `auto` capability resolution.
- Bound queues and message sizes so event traffic cannot grow memory without limit.

**Non-Goals:**

- Implement durable retention, offsets, leases, or redelivery in this change.
- Automatic workflow resumption on another node.
- Exactly-once processing.
- Event-triggered `schedule.on`.
- External HTTP event ingress.
- Per-handler delivery configuration or mixed-capability clusters.
- Replacing the existing scheduler or child-result paths.

## Decisions

### D1: Three seams, not one event bus

The design separates:

1. `EventBus` — the node-facing port used by the interpreter. Publish, subscribe with `auto`, acknowledge, detach, delete.
2. `EventPlane` — the gateway-side extension contract. Publish, add/attach/detach/delete subscription, acknowledge, deliver to a local subscription.
3. `GatewayTransport` — the byte-oriented gateway-to-gateway transport used by the event-plane extension.

Rationale: the interpreter must not know about gateways or sockets; the event-plane extension must not know about nodes or workflow filters; a future external backbone must be able to replace the distributed plane without touching the node edge, and a future direct mesh must be able to replace the transport without touching the planning algorithm.

Alternatives considered:

- One combined `EventBus` interface spanning node and gateway: rejected because it leaks transport and gateway concepts into the handler API.
- Making the transport part of the event-plane extension: rejected because public extension authors would need socket and io_uring access, and the framework would not be able to provide a reusable primitive.

### D2: The event-plane extension is gateway-side; the node edge is core

The framework owns the node event client, TLV frames, the gateway edge, and the mapping from subscription ids to node connections. The extension owns summaries, cross-gateway routing, and local matching. This keeps the node io_uring path in the framework and the distributed algorithm in the extension.

An external-backbone extension is then a gateway-side adapter that publishes and subscribes to the backbone and calls `host.Deliver`. It ignores `GatewayTransport`. A self-hosted extension is a gateway-side plane that uses its own log, metadata store, and replication protocol, potentially over a different transport.

### D3: v1 uses a node relay, not a gateway mesh

The v1 `GatewayTransport` implementation sends an opaque relay frame to a chosen node, which forwards it to other gateway connections. This is possible because every gateway is connected to every node.

```
gateway A ---> relay node ---> gateway B
                   |
                   +---------> gateway C
```

Rationale: no gateway discovery, no gateway listener, no peer reconnect state, and no new external dependency. The framework addition is a gateway identity handshake plus one relay frame handler.

Alternatives considered:

- Direct gateway mesh: requires discovery, membership, reconnect, and a new gateway listener. Larger framework surface.
- Full event broadcast through nodes: wastes node bandwidth and scales poorly; the summary-routing design avoids it.
- Consistent-hash gateway sharding: more scalable but requires a hash ring and membership changes; deferred.

### D4: Gateway identity and relay forwarding

Each gateway announces a `gateway_id` and its event plane's strongest delivery mode on every accepted node connection (`kGatewayHello`). A gateway without an event plane sends no handshake, so the node treats that connection as event-incapable and fails `emit`/`listen` locally instead of hanging. The node stores `gateway_id -> connection` (plus the delivery mode) and forwards `kPeerRelay` frames to the named target connections, or to all other gateway connections for an empty target list. The node never parses the event-plane payload.

The gateway chooses any live node as the relay. The relay frame carries the target ids and payload; the node excludes the source connection. A missing target connection drops only that copy.

The node's existing child-forward path and the relay share one node-side `GatewayConnectionPool` pool, with `GatewayClient` becoming a user of that pool. This preserves child-forward behavior while giving the relay the same live-connection view.

### D5: Coarse routing at the gateway, full filtering at the listener

The event plane routes on CloudEvent context attributes (`type`, `source`, `subject`) and a match-all marker. The full DSL filter is evaluated at the listener: `with.data` predicates, `correlate`, `all`/`any`/`one`/`until`, and `foreach`.

Rationale: the gateway must not depend on jq or the Open Workflow model. Data predicates are application logic; a backend-agnostic event plane cannot evaluate them. Delivering the full envelope also supports `read: envelope` and correlation.

Events that match the coarse pattern but fail the workflow-level filter are acknowledged and skipped, so a durable implementation can advance its offset.

### D6: v1 algorithm is subscription-summary gossip

Each gateway keeps only its local subscriptions and computes a route summary:

- exact `type` values;
- exact `source` values;
- a match-all marker for subscriptions without a type or source constraint.

The summary is broadcast on change and periodically, with a TTL. A publisher sends an event to a peer only when the peer's summary matches. The receiving gateway matches the event against its local subscriptions and calls `host.Deliver` with the full envelope.

This scales with the event vocabulary, not the listener count. A gateway hosting many listeners for one type has a small summary. A future high-cardinality vocabulary can replace the summary representation (hashed summaries, Bloom filters, sharded indexes) without changing the core contracts.

Alternatives considered:

- Broadcast every event to every gateway: simple but O(gateways) per event and wasteful.
- Replicate every subscription to every gateway: accurate but grows with listener count.
- Keyed sharding: more scalable but requires a membership ring and rebalancing; deferred.

### D7: CloudEvent envelope carries opaque data

The event model is a proto with `specversion`, `id`, `source`, `type`, `subject`, `time`, `datacontenttype`, `dataschema`, `bytes data`, and a `map<string,string> attributes` for CloudEvents extensions. `data` is opaque at the bus boundary; the interpreter parses it as JSON when needed.

The bus provides defaults for `specversion`, `id`, `time`, and JSON `datacontenttype`. `(source, id)` is the stable identity. Extension attributes are required for `correlate`, which can reference attributes outside the standard set.

### D8: Emit semantics

The `emit` step:

1. transforms the task input according to `input.from`;
2. evaluates the `data` value under strict-mode rules;
3. builds the CloudEvent with literal context attributes and the event-plane defaults;
4. publishes and suspends until the event plane confirms completion;
5. uses its transformed task input as the raw output;
6. applies `output.as`, `export.as`, and `then` through the ordinary pipeline.

Literal context attributes are a deliberate v1 restriction: the parser models them as strings, and only `data` is a value tree. A future version can widen expression support without changing the event envelope.

The raw output is the transformed input, not the emitted envelope. This keeps `emit` a side-effect step and preserves the data chain; a workflow that needs the envelope can construct it explicitly with a `set` task.

### D9: Listen semantics

The `listen` step derives a coarse subscription pattern from its filters, subscribes with `auto`, and suspends.

- `one` completes on the first matching event.
- `all` completes when every filter has matched at least once.
- `any` completes on the first matching event, or continues until an `until` expression over the accumulated events or a nested consumption strategy is satisfied.
- `read: data` (default) selects the event data; `read: envelope` selects the full envelope.
- Without `foreach`, the consumed events are exposed as a JSON array: arrival order for `one`/`any`, filter declaration order for `all`.
- With `foreach`, the body runs per consumed event with `item`/`at` bindings, starting from the selected value; each iteration applies `foreach.output.as` and `foreach.export.as`; the listen output is the array of iteration results.
- The subscription is deleted on completion, failure, or abandonment.

The exact filter-matching rules are specified in `specs/openworkflow-interpreter/spec.md`; the design keeps the matching in the interpreter so the event plane stays DSL-agnostic.

### D10: Acknowledgement and detach/delete are v1 seams

Every delivery carries an opaque token. The listener acknowledges after processing the event, including non-matching events. A node acknowledgement travels to the gateway edge in `kEventAck`, which forwards the token to the extension's acknowledge operation; in v1 that operation is a no-op. The event-plane contract distinguishes detach (connection lost) from delete (explicit termination); the v1 summary-mesh extension treats detach as delete, while a durable extension can preserve the offset.

Subscription identity is stable: workflow identity + step path + activation index. This costs little now and is required for v2 reattachment.

The gateway edge answers a subscription request with `kEventSubscribeResult` (acceptance plus the delivery mode resolved by the extension), so the node reports a subscription ready only after the edge accepted it and with the actual guarantee. The node event bus watches its gateway connection and fails a ready subscription with `Unavailable` when the connection is lost. The `ListenFrame` is written to tolerate duplicate and out-of-order events by event identity, even though v1's relay is at-most-once. This keeps v2 from requiring an interpreter rewrite.

### D11: v2 durability is a new extension behind the same seam

The v2 target is durable at-least-once. The intended implementation order is:

1. an external clustered backbone extension (NATS JetStream or similar), which maps durable subscriptions, offsets, redelivery, and retention directly;
2. a self-hosted replicated log extension, if zero external dependencies is required later.

The contract already supports both: offsets and tokens are opaque, the extension owns retention and leases, and the core edge only moves events and acknowledgements. The v2 defaults are a lease TTL plus bounded time/size retention; the v2 extension owns those policies.

Exactly-once remains a workflow-runtime property. It requires transactional workflow state, deterministic replay, and idempotent side effects; the event plane only provides at-least-once.

### D12: Configuration stays minimal

`GatewayConfig.event_plane` selects the extension and carries the gateway identity. It is optional: without it the gateway still runs tasks and nodes, while emit/listen fail with an unsupported-capability status. There is no per-handler delivery mode; the event-plane extension is the single source of truth, and the uniform event plane assumption makes capability fallback unnecessary.

The v1 relay needs no peer addresses, listener port, or discovery configuration. The complete-bipartite topology is the configuration.

### D13: New frames are built-in cases, not scheduler claims

`kGatewayHello`, `kPeerRelay`, `kPeerRelayDelivery`, `kEventPublish`, `kEventSubscribe`, `kEventUnsubscribe`, and `kEventDelivery` are handled by the core TLV handlers, beside node advertisements and results. They are not scheduled through the scheduler seam because they are not scheduling-protocol frames. The scheduler seam remains for schedulable protocols.

### D14: Bounded queues and drop policy

The event plane is best-effort, so it must fail fast rather than grow without bound. The design bounds:

- the number of in-flight events per subscription at the node edge;
- the relay payload size;
- the per-connection relay queue;
- the pending event queue inside a `ListenFrame`.

When a bound is reached, the event is dropped with a log/metric. A slow `foreach` body therefore causes drops, not memory growth. v2's durable extension owns its own retention and flow control.

### D15: Failure status mapping

- Malformed emit/listen document or expression: `TASK_STATUS_MALFORMED_REQUEST`, before any publish or subscribe.
- Gateway live but no event plane configured: `TASK_STATUS_UNIMPLEMENTED`, decided locally by the node from the missing handshake.
- No event path (no live gateway) or lost subscription: `TASK_STATUS_UNAVAILABLE`.
- Unsupported guarantee (explicit request only): `TASK_STATUS_UNIMPLEMENTED`.
- Runtime predicate/processing failure: `TASK_STATUS_INTERNAL`.

### D16: Testing strategy

Unit tests:

- CloudEvent defaults and identity;
- summary computation, TTL expiry, targeted routing, match-all;
- relay frame forwarding, source exclusion, missing-target drop, payload size and queue bounds;
- listener filter/correlation, `one`/`all`/`any`/`until`, `foreach`, `read`, acknowledgement of non-matching events;
- emit defaults, data evaluation, output pipeline, publish failure;
- subscription lifecycle: complete, cancel, abandon, connection loss, duplicate id;
- validation of malformed emit/listen documents.

Integration tests:

- emit on node A, listen on node B through two gateways and a relay node;
- two listeners on different gateways both receive the same event;
- a listener that subscribes after an event misses it (v1 best-effort);
- a node connection loss detaches and deletes the ephemeral subscription;
- a competing fork abandons a listen branch and drops later events.

## Sequence Diagrams

### Cross-gateway emit and listen

```
Node A            Gateway A          Relay node          Gateway B          Node B
  |                  |                   |                   |                  |
  | emit             |                   |                   |                  |
  | kEventPublish    |                   |                   |                  |
  |----------------->|                   |                   |                  |
  |                  | B summary matches |                   |                  |
  |                  | kPeerRelay([B])   |                   |                  |
  |                  |------------------>|                   |                  |
  |                  |                   | kPeerRelayDelivery|                  |
  |                  |                   |------------------>|                  |
  |                  |                   |                   | match local sub  |
  |                  |                   |                   | kEventDelivery   |
  |                  |                   |                   |----------------->|
  |                  |                   |                   |                  | filter
  |                  |                   |                   |                  | process
  |                  |                   |                   |                  | ack
```

### Listen subscription setup

```
Node B            Gateway B edge     EventPlane ext      peers
  |                  |                    |                |
  | kEventSubscribe  |                    |                |
  |----------------->| AddSubscription    |                |
  |                  |------------------->|                |
  |                  |                    | update summary |
  |                  |                    | Broadcast      |
  |                  |                    |--------------->|
  |                  | ready(actual_mode) |                |
  |<-----------------|<-------------------|                |
  | suspend          |                    |                |
```

### Future durable at-least-once

```
Node B            Gateway B edge     Durable extension    Backbone/log
  |                  |                    |                   |
  | subscribe(auto)  |                    |                   |
  |----------------->| create durable sub |                   |
  |                  |------------------->| create consumer   |
  |                  |                    |------------------>|
  |                  | ready(at_least_once)|                  |
  |<-----------------|<-------------------|                   |
  |                  |                    | fetch events      |
  |                  |                    |<------------------|
  | kEventDelivery   |                    |                   |
  |<-----------------|                    |                   |
  | process          |                    |                   |
  | ack(token)       |                    |                   |
  |----------------->| Ack                |                   |
  |                  |------------------->| commit offset     |
  |                  |                    |------------------>|
```

## Risks / Trade-offs

- **Complete-bipartite invariant breaks** → a relay node cannot reach a target gateway. Mitigation: document the invariant, drop only the unreachable target, and allow a future transport replacement. The event plane does not assume the relay forever.
- **Summary propagation race** → a listener can miss events published before its summary reaches the publisher. Mitigation: accepted v1 best-effort semantics; the v2 durable extension removes the race for subscribed listeners.
- **Relay node hotspot or failure** → relay traffic concentrates on one node per operation. Mitigation: spread relay-node selection across live nodes, bound queues, and allow an alternative transport.
- **Unbounded outbound queues** → event storms or slow listeners can grow memory. Mitigation: per-subscription and per-connection bounds with drop policy and metrics. This also addresses the existing unbounded-mailbox follow-up for event traffic.
- **Large CloudEvents** → TLV accumulates large values in memory. Mitigation: cap relay and event payload sizes and reject oversized events.
- **No ordering guarantee** → cross-publisher and cross-peer ordering is undefined. Mitigation: document it; a future durable extension can add partition-key ordering if needed.
- **Uniform event plane assumption** → v1 and v2 gateways must not coexist. Mitigation: the event plane is a cluster deployment choice; mixed clusters are explicitly out of scope.
- **No TLS or authentication on relay traffic** → the relay trusts gateway peers. Mitigation: internal network assumption for v1; TLS is a separate platform feature.
- **Listen semantics ambiguity** → `all`/`any`/`until`/`foreach` are subtle. Mitigation: the specs pin the behavior and the interpreter tests cover each strategy; the design keeps the event plane out of these decisions.
- **Duplicate delivery in v2 after a crash** → at-least-once can repeat a `foreach` side effect. Mitigation: expose `(source, id)` to the workflow for idempotency; exactly-once requires transactional workflow state and is deferred.

## Migration Plan

1. Land the core contracts, CloudEvent proto, TLV frames, and the node relay transport behind an optional `event_plane` configuration. Existing deployments without the section continue to run; only emit/listen fail.
2. Land the bundled summary-mesh extension and the openworkflow emit/listen implementation. Enable `event_plane` on every gateway in a cluster; the event plane is uniform by construction.
3. Rollback removes the `event_plane` section or restores the previous binary. v1 has no persistent state, so rollback has no data migration; workflows using emit/listen fail rather than hang.
4. v2 durable at-least-once is a separate change: introduce the durable extension, keep the same node edge and interpreter, and add lease/retention configuration to the extension's typed config.

## Open Questions

- The exact default values for summary TTL, relay payload cap, per-subscription in-flight cap, and per-connection relay queue cap are operational tuning knobs and can be set during implementation.
- Whether to expose the actual delivery guarantee in workflow result metadata is deferred; v1 logs it at subscription time.
- The v2 lease TTL and retention defaults belong to the durable extension change, not this one.
