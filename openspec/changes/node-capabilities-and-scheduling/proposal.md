## Why

Strij currently hardcodes node addresses in gateway config: `NodeDiscovery` (static only) fires a one-shot callback that is not wired to `NodeDirectory`, which is constructed once and routes via round-robin with no knowledge of a node's capabilities or load. This cannot support the project's core goals — thousands of nodes, millions of tasks, no single points of failure, multiple parallel gateways, and pluggable scheduling policies. Nodes need to be discovered dynamically, advertise what they can run (hardware pools, handler capacity), report how loaded they are, and let gateways route based on that — all without predefining node capabilities in gateway config.

## What Changes

- **Node identity**: `NodeInfo` gains a stable `node_id`, distinct from address, so nodes can be re-announced, reconnected, and reconciled across channels without address churn.
- **Dynamic `NodeDirectory`**: add/remove/update of `Node` objects at runtime, with reconciliation against discovery snapshots (deltas applied on each callback). Discovery callbacks may fire repeatedly.
- **Node advertisement frame** (`kNodeAdvertisement`): nodeagent pushes its node plane over the task connection on connect — `node_id`, address, hardware **pools** (named, e.g. `cpu`, `mem`, `gpu.h100`), **reservations** (handler-pinned pools, excluded from shared capacity), per-type handler capabilities (concurrency, `function_sourced`), `update_channels` (heartbeat in v1), and `scheduling_protocols` (`push` in v1).
- **Named-pool resource model**: `ResourceRequirements` as `map<string, uint64>` keyed by pool name; the seam for future function-level requirements (a `RequirementsResolver` interface with a params-only default) is sketched; the function repository is **out of scope**.
- **Node state reporting** (`kNodeState`): periodic snapshots from nodeagent to gateway over the task connection (heartbeat channel), driving a gateway-side state tracker with **exact accounting** for the centralized single-gateway topology.
- **Nodeagent admission control**: pool + per-type concurrency accounting; when capacity is exhausted the nodeagent rejects with a `kTaskRejected` frame. The gateway treats rejection as a safety net.
- **Pluggable scheduler**: a `Scheduler` extension family (Registry pattern). v1 ships `round_robin` (preserving current behavior) and `capability_aware` (filters by pools/capacity/state). Each scheduler declares a required `scheduling_protocol`; gateway excludes nodes that don't advertise it. Two-sided algorithms (e.g. probe/lease scheduling) are **reserved for the future** via the `scheduling_protocols` seam — no protocol break needed.
- **Config**: `GatewayConfig.scheduler` (ExtensionConfig); `NodeAgentConfig` gains pools, reservations, handler capabilities and activates the reserved `heartbeat_interval` as the state-snapshot cadence.
- **Protocol**: new TLV type_ids `kNodeAdvertisement`, `kNodeState`, `kTaskRejected`; `TaskRejected` message in `task.proto`.

## Capabilities

### New Capabilities
- `node-advertisement`: the node plane carried in `kNodeAdvertisement` — pools, reservations, handler capabilities, `update_channels`, `scheduling_protocols`, and the named-pool `ResourceRequirements` model plus the `RequirementsResolver` seam.
- `dynamic-node-directory`: runtime add/remove/update of nodes in `NodeDirectory`, reconciliation against discovery snapshots, stable node identity.
- `node-state-reporting`: `kNodeState` heartbeat snapshots, gateway state tracking with exact accounting, nodeagent pool/concurrency admission and the `kTaskRejected` path.
- `pluggable-scheduler`: the `Scheduler` extension family, `scheduling_protocols`-based candidate filtering, and the v1 push-based policies.

### Modified Capabilities
- `node-discovery`: `NodeInfo` gains `node_id`; the discovery callback becomes a repeatable snapshot that the gateway reconciles into `NodeDirectory`.
- `node-directory`: runtime add/remove/update; per-node capability and state storage; selection delegated to a scheduler instead of hardcoded round-robin.
- `typed-tlv-messages`: new TLV type_ids `kNodeAdvertisement`, `kNodeState`, `kTaskRejected`.
- `task-protocol`: new `TaskRejected` message schema.
- `gateway-config`: new `scheduler` ExtensionConfig field.
- `nodeagent-config`: new pools/reservations/handler-capabilities fields; `heartbeat_interval` becomes the active state-reporting cadence.

## Impact

- **Gateway core**: `Node`, `NodeDirectory`, `GatewayTlvHandler`, `GatewayHttpHandler`, `gateway.cc` (wire discovery→directory, install scheduler).
- **Nodeagent core**: `NodeagentTlvHandler` (advertisement + state frames, admission), new state reporter, `TaskHandlerManager` capability aggregation.
- **Extensions**: `NodeInfo` in `node_discovery.hh`; new `scheduler` extension category; new `scheduling_protocols` agreement seam.
- **Protos**: new node capabilities proto (e.g. `api/core/node/capabilities.proto`), `TaskRejected` in `task.proto`, `GatewayConfig.scheduler`, nodeagent pools/reservations/handlers + active `heartbeat_interval`.
- **Protocol**: `TlvFrame` type_ids + `TlvParser` unchanged (frame types are consumer-interpreted).
- **Tests**: new suites for node directory reconciliation, advertisement handshake, state tracking, admission/reject, and scheduler policies; mocks extended with a fake dynamic discovery and fake nodeagent frames.
