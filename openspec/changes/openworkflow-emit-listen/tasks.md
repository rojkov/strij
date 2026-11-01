# Tasks

## 1. Event model and public contracts

- [x] 1.1 Add the CloudEvent proto (specversion, id, source, type, subject, time, datacontenttype, dataschema, bytes data, extension attributes) and its Bazel target; verify a serialization round-trip test covers required and optional fields.
- [x] 1.2 Add the relay, subscription, and delivery protos (`GatewayHello`, `PeerRelay`, `PeerRelayDelivery`, `EventSubscriptionRequest`, `EventUnsubscribe`, `EventDelivery`) and Bazel targets; verify round-trip tests for each message.
- [ ] 1.3 Define the public node `EventBus` and `EventSubscription` contracts with publish, subscribe with `auto`, acknowledge, detach, and delete; verify a fake subscription in a unit test exercises the readiness callback and acknowledgement.
- [ ] 1.4 Define the public gateway `EventPlane`, `EventPlaneFactory`, `EventPlaneDeps`, `EventPattern`, and delivery-mode contracts; verify a fake event plane can be constructed and selected through the registry in a unit test.
- [ ] 1.5 Define the public `GatewayTransport` contract with send, broadcast, and inbound-message operations; verify a fake transport drives an event-plane unit test.
- [ ] 1.6 Add `EventPlaneConfig { ExtensionConfig extension; string gateway_id; }` to `GatewayConfig` and wire YAML loading; verify config tests cover a present, absent, and unpacked event-plane section.
- [ ] 1.7 Add the node `EventBus` reference to `TaskHandlerDeps` and construct it in the nodeagent framework; verify a handler factory test retrieves the event bus at `Create`.

## 2. TLV frames and gateway relay

- [x] 2.1 Add frame constants 11 through 17 to `TlvFrame`; verify a unit test asserts each value and that 0 through 10 are unchanged.
- [ ] 2.2 Generalize the node's outbound gateway pool into a shared `GatewayConnectionPool` service and make `GatewayClient` a user of it; verify existing child-forward tests still pass and a new test covers pruning a dead connection.
- [x] 2.3 Handle `kGatewayHello` on the node and maintain the gateway-id-to-connection map; verify tests cover identity learning, targeted lookup, and deterministic replacement on a repeated handshake.
- [x] 2.4 Handle `kPeerRelay` on the node: broadcast with an empty target list, targeted forwarding, source exclusion, missing-target drop, and payload-size rejection; verify each behavior with a unit test.
- [ ] 2.5 Implement the gateway-side relay transport: choose a live node, write `kPeerRelay`, receive `kPeerRelayDelivery`, and return `Unavailable` when no node is live; verify tests cover successful send, broadcast, no-live-node, and relay-node failure.
- [ ] 2.6 Add the new frame cases to the gateway and node TLV handlers; verify a test asserts event-plane and relay frames do not reach the scheduler router.

## 3. Bundled summary-mesh event plane

- [ ] 3.1 Implement route-summary computation from local subscriptions (exact types, exact sources, match-all) and verify unit tests cover each combination.
- [ ] 3.2 Implement the local subscription registry and `host.Deliver` routing with the full CloudEvent envelope; verify a test delivers to matching local subscriptions and skips non-matching ones.
- [ ] 3.3 Implement publish routing: local delivery plus targeted `GatewayTransport::Send` to peers whose summaries match; verify tests cover local-only, peer-only, and both.
- [ ] 3.4 Implement summary broadcast on change, periodic refresh, peer summary storage, epoch ordering, and TTL expiry; verify tests cover propagation, stale-summary rejection, and expiry.
- [ ] 3.5 Implement the v1 acknowledgement no-op and the detach-versus-delete lifecycle; verify tests distinguish detach from delete at the extension boundary.
- [ ] 3.6 Register the summary-mesh factory and load it from `GatewayConfig.event_plane`; verify startup succeeds for a valid config and fails for an unknown name or bad typed config.

## 4. Node event bus and gateway event edge

- [x] 4.1 Implement the node event client: publish and subscribe over a live gateway connection, readiness callback, and unsubscribe; verify tests cover success and the no-live-gateway failure path.
- [x] 4.2 Implement inbound `kEventDelivery` dispatch to the local subscription and the v1 no-op acknowledgement; verify a test delivers an event and accepts an acknowledgement without durable state.
- [ ] 4.3 Implement the gateway event edge: subscription-id-to-node mapping, subscribe registration, publish forwarding, and delivery to the owning node; verify a test routes an event to the correct node connection.
- [ ] 4.4 Detach gateway subscriptions on node connection loss and delete them on explicit unsubscribe; verify tests cover both paths and assert no later delivery occurs.
- [ ] 4.5 Make emit/listen fail with an unsupported-capability status when no event plane is configured; verify a gateway without `event_plane` still schedules ordinary tasks and rejects event operations cleanly.
- [ ] 4.6 Wire the event plane, relay transport, and node event bus into the gateway and node frameworks; verify `make build` succeeds with no wiring changes needed in existing handler tests.

## 5. Emit task

- [ ] 5.1 Implement emit property evaluation and CloudEvent defaults (specversion, id, time, JSON datacontenttype), keeping context attributes literal; verify unit tests for defaults, authored values, and data expressions.
- [ ] 5.2 Implement the publish path, transformed-input raw output, and the `output.as`/`export.as`/`then` pipeline; verify tests cover success, output shaping, export, and a skipped emit.
- [ ] 5.3 Implement emit validation and failure statuses (`MALFORMED_REQUEST`, `UNAVAILABLE`, `INTERNAL`); verify tests cover a malformed data expression and a publish failure.
- [ ] 5.4 Verify emit works in a `for` body and a fork branch; add tests for both placements and for the branch contribution to the join.

## 6. Listen task

- [ ] 6.1 Implement subscription creation with the stable subscription id and `auto` mode, readiness handling, and sequence suspension; verify a test subscribes, suspends, and resumes on a delivered event.
- [ ] 6.2 Implement filter matching, `with.data` predicates, correlation, and acknowledgement of non-matching events; verify tests cover a matching event, a non-matching event, and a correlation match.
- [ ] 6.3 Implement the `one`, `all`, `any`, and `until` strategies, including the `until` expression and nested-strategy form; verify each strategy with a unit test and check the raw-output ordering.
- [ ] 6.4 Implement `read: data|envelope`, `foreach` body execution, loop-variable bindings, per-iteration output/export, and the array raw output; verify tests for both read modes and for multiple consumed events.
- [ ] 6.5 Implement completion, abandonment, and cancellation of listen subscriptions, plus the lost-subscription `UNAVAILABLE` failure; verify tests for normal completion, competing-fork abandonment, and connection loss.
- [ ] 6.6 Implement listen validation before subscription: filter expressions, `until`, `read`, foreach variable names, and strategy shapes; verify malformed documents fail before any subscribe frame is written.
- [ ] 6.7 Add duplicate tolerance by `(source, id)` in `ListenFrame`; verify a test delivers the same event twice and processes it once.

## 7. Limits, observability, and integration

- [ ] 7.1 Add and enforce bounds for in-flight events per subscription, relay payload size, relay queue depth, and the pending-event queue in `ListenFrame`; verify tests cover rejection and drop behavior.
- [ ] 7.2 Log subscription readiness with the actual delivery mode, dropped events, and relay failures; verify the log paths are exercised in the tests.
- [ ] 7.3 Add an end-to-end test with two gateways, a relay node, and two nodes: emit on one node, listen on the other, and assert delivery with the expected CloudEvent attributes.
- [ ] 7.4 Add an integration test that asserts a listener subscribing after an event is emitted misses it (v1 best-effort) and does not hang.
- [ ] 7.5 Add the example gateway/nodeagent configuration with `event_plane` and verify `--validate_only` passes for both binaries.
- [ ] 7.6 Run `make build`, `make test`, and `make check`; verify all targets pass and no existing scheduler, child-task, or task-handler test regresses.

## 8. Durable-evolution seams

- [ ] 8.1 Add a fake at-least-once event-plane implementation in tests and verify the node edge and interpreter use the acknowledgement and detach/delete seams without changes; verify no-op best-effort behavior still passes.
- [ ] 8.2 Verify the bundled summary mesh reports best-effort through `auto` and rejects an explicit at-least-once request with `UNIMPLEMENTED`; add a test for both outcomes.
- [ ] 8.3 Add a test that reattaches the same stable subscription id and does not create a second independent subscription; verify the fake durable plane observes reattachment.
- [ ] 8.4 Add a follow-up note to `ROADMAP.md` for the v2 durable at-least-once extension (external backbone first, self-hosted replicated log later, lease TTL plus bounded retention); verify the note names the unchanged node edge and interpreter contracts.
