# Proposal

## Why

The Open Workflow parser already models `emit` and `listen` tasks, but the interpreter rejects them as unsupported and Strij has no event plane. A workflow therefore cannot publish a CloudEvent or wait for one, and a listener on one node cannot observe an event emitted from a node attached to a different gateway. This change adds the event plane and both task kinds, with a best-effort v1 implementation and contracts designed so a durable at-least-once backend can be added later without changing the interpreter or the node edge.

## What Changes

- Introduce a CloudEvent envelope and a node-facing `EventBus` port: publish, subscribe with an `auto` delivery mode, acknowledge, detach, and delete.
- Add a gateway edge that owns the mapping from logical subscriptions to node connections. Connection loss detaches a subscription; explicit cancellation or completion deletes it.
- Add a gateway-side `EventPlane` extension category with a backend-agnostic contract: publish, create/attach/detach/delete subscription, acknowledge, and deliver to a local subscription. The event-plane extension is the single source of truth for the delivery guarantee; the openworkflow handler asks for `auto` and does not configure a guarantee.
- Add a `GatewayTransport` byte-transport contract for gateway-to-gateway communication. v1 implements it with a node relay over the existing gateway connections: a gateway handshake, opaque relay frames, and the complete-bipartite assumption that every gateway connects to every node.
- Implement the bundled v1 event plane as subscription-summary gossip: local subscriptions, type/source route summaries, periodic re-broadcast with a TTL, targeted event forwarding, and local matching. It is best-effort, at-most-once, and retains nothing.
- Implement `emit` and `listen` execution in the openworkflow interpreter: CloudEvent construction and defaults, filters and correlation, `one`/`all`/`any`/`until`, `foreach` with loop variables, `read: data|envelope`, validation, suspension, cancellation, acknowledgement of non-matching events, and failure statuses.
- Add the durable-evolution seams now: stable subscription identity, a delivery token and acknowledgement call site, detach versus delete, duplicate tolerance, and `auto` capability resolution.
- Add an optional `GatewayConfig.event_plane` extension selection and gateway identity for the relay handshake. Add the node `EventBus` to `TaskHandlerDeps`.
- Define the v2 durable at-least-once follow-up: an external clustered backbone extension first, with a self-hosted replicated log remaining possible behind the same `EventPlane` and `GatewayTransport` seams. It will use lease TTL plus bounded time/size retention. It is not implemented by this change.
- Do not change the DSL parser: `emit` and `listen` already parse.

**Non-goals:** automatic workflow resumption across nodes, exactly-once processing, event-triggered `schedule.on`, external HTTP event ingress, per-handler delivery configuration, and mixed-capability clusters.

## Capabilities

### New Capabilities

- `event-plane`: the CloudEvent envelope, the node-facing `EventBus`, the gateway edge, the `EventPlane` extension contract, the v1 subscription-summary event plane, delivery semantics, subscription lifecycle (acknowledge/detach/delete), and the durable-evolution seams.
- `gateway-relay`: the v1 gateway-to-gateway byte transport over the node relay, including the gateway handshake, relay frames, the complete-bipartite assumption, and the `GatewayTransport` contract.

### Modified Capabilities

- `openworkflow-interpreter`: execution, validation, suspension, flow/output/export, cancellation, and acknowledgement behavior for `emit` and `listen` tasks.
- `typed-tlv-messages`: new event-plane and gateway-relay frame type IDs and payloads.
- `nodeagent-task-handlers`: `TaskHandlerDeps` exposes the node `EventBus` to handlers that consume events.
- `extension-api-surface`: the new gateway event-plane extension category and its dependency bundle, plus the public `EventPlane` and `GatewayTransport` contracts.
- `gateway-config`: optional event-plane extension selection and gateway identity for the relay handshake.

## Impact

- New API and config: CloudEvent protobuf, event-plane extension config, new TLV frames, `EventPlane`/`GatewayTransport`/`EventBus` contracts.
- Modified code: openworkflow handler, interpreter, and frames; node TLV handler and node framework; gateway TLV handler and gateway framework; gateway config schema; extension loader.
- New tests: summary gossip and TTL, relay forwarding and source exclusion, `emit`/`listen` semantics, subscription lifecycle, acknowledgement no-op in v1, duplicate tolerance, and failure statuses.
- Unchanged: schedulers, child-result routing, task protocol, task-result status, node discovery, and the DSL parser.
- Follow-up: the v2 durable at-least-once extension (external backbone first, self-hosted later); exactly-once remains a workflow-runtime concern.
