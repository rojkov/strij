## Context

Today a Strij gateway learns about nodeagents only from `node_discovery` (a `NodeDiscovery` extension; only `static` exists). The discovery callback delivers a one-shot `std::vector<NodeInfo>` snapshot; `gateway.cc` copies it into a local vector and constructs a fixed `NodeDirectory` from it. There is no wiring that lets later discovery events add or remove nodes, and `NodeInfo` carries only `{address}`. `NodeDirectory::GetNextNode()` selects round-robin over connected nodes with no notion of what a node can run or how busy it is. The nodeagent is purely a task server: it accepts `kTaskSubmission` frames, routes by `task.type()` through `TaskHandlerManager`, and returns `kResult` frames. `kHeartbeat` exists as a type_id but carries nothing and does nothing. There is no reverse channel (nodeagent→gateway) beyond results.

This design introduces the node lifecycle and capability-aware routing spine: dynamic membership, a node capability advertisement, state reporting, admission control, and a pluggable push-based scheduler. It deliberately reserves seams for two future axes — the function repository and two-sided (probe-based) scheduling — so those can land without a protocol break.

## Goals / Non-Goals

**Goals:**
- Make node membership dynamic: `NodeDirectory` adds/removes/updates nodes from repeated discovery snapshots; nodes have a stable identity.
- Give gateways a per-node capability view (hardware pools, handler capacity) obtained from the nodeagent itself, not from gateway config.
- Give gateways a current-state view (pool usage, in-flight tasks) that is exact for the centralized single-gateway topology.
- Let nodeagents be the enforcement point: pool/concurrency admission with a rejection path.
- Make scheduling a pluggable extension with a round-robin policy (current behavior preserved) and a capability-aware policy.
- Define seams (`update_channels`, `scheduling_protocols`) so future state channels and two-sided schedulers slot in without protocol changes.

**Non-Goals:**
- Function repository / real `RequirementsResolver` and `CodeResolver` implementations (only the seam + params-only default).
- New discovery *channels* (Redis, mDNS, ZooKeeper, direct announcement) — the mechanism supports them; only `static` ships.
- Nodeagent-side `NodeAnnouncer` existence extensions and inbound (nodeagent-initiated) connections (orientation option C).
- Probe/lease/pull two-sided scheduling (Sparrow-style) — reserved behind `scheduling_protocols`.
- State channels other than heartbeat-over-connection.
- Reconnection logic for disconnected nodes (existing roadmap item; this change handles removal, not reconnect-backoff).

## Decisions

### D1 — Thin discovery channel

The discovery channel (the `NodeDiscovery` extension) carries **existence only**: `node_id` + `address`. Everything else — capabilities, state — rides the task connection the gateway opens anyway. On connect, the nodeagent pushes a `kNodeAdvertisement` frame; periodically it pushes `kNodeState` frames.

- *Why*: capabilities and state are authoritative only when they come from the nodeagent itself; a registry copy is always at risk of staleness. It also means the capability proto is transported over one channel (the task connection), not re-encoded per discovery backend (Redis blob, mDNS TXT, TLV).
- *Alternative considered*: thick channel (capabilities stored in the discovery source). Rejected: three encodings of the same proto, staleness, and the gateway could not verify anything anyway — the nodeagent is the only honest source.
- *Consequence*: a node's capabilities are known only after the gateway connects. Acceptable: the gateway must connect to route tasks anyway.

### D2 — Two planes of information

Information about what a node can run is split by scope and lifetime:

```
function plane (global)                      node plane (per node)
  (type, function_id) → reqs    ◀──future──  node_id, address
  (type, function_id) → code                  pools, reservations
  vetting flags                               handlers: type, concurrency,
                                              function_sourced
                                              update_channels, scheduling_protocols
  shared source (repo)                        carried in kNodeAdvertisement
```

The gateway scheduler combines three orthogonal inputs at decision time: **node can run type T** (advertisement) ∧ **function f needs R** (repo, deferred) ∧ **pools have room** (state). "Both sides agree on capabilities" is a shared-read property of the repo, not a negotiation protocol. This change builds the node plane; the function plane is a sketched seam (D13).

### D3 — Node identity

Each nodeagent generates a stable `node_id` at startup (reuse the readable-id scheme from `readable-task-id-generation`). It is the map key in `NodeDirectory`, distinct from `address` (addresses change under DHCP/containers/NAT; a node can be re-announced under a new address with the same identity). Announcements and state frames carry `node_id`; the gateway keys per-node records by it.

- *Why*: address-as-key breaks reconciliation across channels and reconnects.
- *Alternative*: persist node_id across restarts. Deferred; in-memory generation is fine for v1.

### D4 — Dynamic NodeDirectory with snapshot reconciliation

`NodeDiscovery::DiscoveryCallback` becomes a **repeatable full-snapshot** event: every invocation carries the complete current node set. `NodeDirectory` diffs the snapshot against its current membership:

```
DiscoverySource                         NodeDirectory
  │── snapshot {A, B} ─────────────────▶│  add A, add B (connect)
  │── snapshot {A, C} ─────────────────▶│  keep A, remove B, add C (connect)
```

- Add: construct `Node`, start connect.
- Remove: disconnect and drop the `Node` (no reconnect in v1).
- Update: if `address` or `node_id` changed for an existing id, reconnect the record.

`NodeDirectory` gains `AddNode`, `RemoveNode`, `Reconcile(snapshot)`, and accessors that the scheduler queries. Membership is always driven by the discovery source; the gateway never invents nodes.

- *Why snapshots, not events*: snapshots are loss-tolerant (a missed event self-heals on the next snapshot), which suits unreliable channels (mDNS, Redis). Events would require exactly-once delivery.
- *Static discovery* continues to fire exactly once, so `static` behaves as today — only the plumbing changes.

### D5 — Advertisement handshake and payload

The nodeagent sends `kNodeAdvertisement` (protobuf `NodeCapabilities`) as the **first frame on every accepted connection**; no request/negotiation needed because every nodeagent connection is a gateway connection in v1.

```
gateway                              nodeagent
  │── TCP connect ──────────────────▶│
  │◀── kNodeAdvertisement ───────────│  node_id, address, pools, reservations,
  │                                  │  handlers, update_channels,
  │                                  │  scheduling_protocols, capability_version
  │── kTaskSubmission ──────────────▶│  Task
  │◀── kResult / kTaskRejected ──────│  (as admitted)
  │◀── kNodeState (periodic) ────────│
```

`NodeCapabilities` (package `strij.node`):

```proto
message NodeCapabilities {
  string node_id = 1;
  string address = 2;                     // where the gateway connects (outbound)
  repeated ResourcePool pools = 3;        // named, shared, schedulable capacity
  repeated PoolReservation reservations = 4; // handler-pinned, excluded from shared
  repeated HandlerCapability handlers = 5;
  repeated UpdateChannel update_channels = 6;   // v1: [heartbeat]
  repeated SchedulingProtocol scheduling_protocols = 7; // v1: [push]
  uint32 capability_version = 8;
}
```

The gateway stores the advertisement on the owning `Node` (the per-connection handler is wired to its `Node`), validates `capability_version`, and warns (not rejects) on mismatch in v1.

### D6 — Named-pool resource model

`ResourceRequirements` is a `map<string, uint64>` keyed by **pool name**. Units are a convention of the name (`cpu` → cores, `mem` → bytes, `gpu.h100` → devices). A node announces the pools it can schedule; a handler/function draws from them; admission checks `free(pool) ≥ req(pool)`.

```proto
message ResourceRequirements {
  map<string, uint64> resources = 1;   // {"cpu": 2, "mem": 2147483648, "gpu.h100": 1}
}

message ResourcePool {
  string name = 1;
  uint64 total = 2;
}

message PoolReservation {
  string task_type = 1;
  string pool = 2;
  uint64 amount = 3;                    // permanently pinned to this handler
}
```

- *Why named pools*: extensible without schema churn — new hardware (e.g. `gpu.h100`) is a new pool name, not a new proto field. GPU model specificity is a name, so reserving a specific accelerator is just a pool reference.
- Reservations let handler-pinned resources be **excluded** from the shared capacity the gateway routes on (`shared_free = total − reservations − in_use`), per the "handler GPUs are not announced as general capacity" requirement.
- *Alternative considered*: typed `cpu`/`mem`/`gpu` fields. Rejected: not extensible to new accelerators; every new hardware type needs a schema release.

### D7 — Update-channel seam

The advertisement declares how the nodeagent will deliver node state via `update_channels`. The descriptor is self-describing so the gateway needs no predefined channel config. v1 defines exactly one channel kind, `heartbeat` (state over the task connection), which is the default and needs no extra gateway machinery. Redis/ZK channels are future: their descriptors carry endpoint/channel names, and the gateway instantiates a matching receiver (or warns and falls back to heartbeat).

- *Why this seam now*: the advertisement is the only agreement point between node and gateway; adding a field later is a protocol change that would touch every nodeagent. Naming it now (with one valid variant) costs nothing.

### D8 — Scheduling-protocol seam and candidate filtering

`NodeCapabilities.scheduling_protocols` lists the task-flow protocols the nodeagent's *admission* speaks. `push` is built in (accept `kTaskSubmission`, run admission) and is the entire v1 task path. A future two-sided scheduler is implemented as a **nodeagent-side extension** that registers its protocol handler (e.g. `probe`, implementing `TaskProbe`/reservation/lease/`TaskRequest` frames) and adds it to its advertisement.

On the gateway, each `Scheduler` extension declares a `required_protocol`. Nodes that do not advertise it are excluded from that scheduler's candidate set:

```
gateway Scheduler "capability_aware"   requires "push"
  candidates = all connected nodes

future Scheduler "sparrow"             requires "probe"
  candidates = nodes advertising "probe" (push-only nodes are ignored,
              or a degraded-mode fallback tier at the scheduler's choice)
```

This gives mixed-fleet rollout: a new nodeagent scheduling extension can be deployed on a subset of nodes while the gateway routes around the rest.

### D9 — State reporting (heartbeat channel)

The nodeagent maintains per-pool usage counters and per-type in-flight counts from admission/completion, and multicasts a `kNodeState` snapshot on **every established gateway connection** every `heartbeat_interval` (Duration, default 10s — the field is already reserved in `NodeAgentConfig` with validation against `connection_timeout`). Snapshot (not delta) — loss-tolerant and self-healing; the frame is small.

```proto
message NodeState {
  string node_id = 1;
  uint64 seq = 2;                       // monotonic, for coalescing
  uint64 timestamp = 3;
  repeated PoolUsage pools = 4;         // {pool, in_use}
  uint64 in_flight = 5;                 // node-wide
  repeated TypeUsage type_usage = 6;    // {task_type, in_flight}
}
```

The gateway's `StateTracker` consumes these. In v1 state rides the connection, so a live connection *is* liveness: the existing TCP-drop handling covers eviction, and `kNodeState` staleness is not separately enforced (a future non-connection channel will need a TTL/eviction rule — noted in Open Questions).

### D10 — Exact vs cached state (dual-mode)

Strij must scale both directions. The protocol is uniform; the **state model is pluggable** and the mode is a deployment choice:

| | exact (centralized) | cached (distributed, future) |
|---|---|---|
| state model | `ExactStateTracker`: gateway counts every send + completion itself, corrected by `kNodeState` snapshots | cached estimates + TTL, corrected by rejections |
| scheduler | route only to a known-free node (no probing) | probe-based, self-selection |

v1 ships `ExactStateTracker`. It is authoritative for a single gateway because the gateway observes every task it routes and every completion (and every rejection). `kNodeState` snapshots are used for verification and drift correction rather than as the primary signal.

### D11 — Nodeagent admission control and rejection

Admission runs on every `kTaskSubmission`: check `shared_free(pool) ≥ task_requirements(pool)` for each requested pool **and** per-type concurrency headroom. On success, account (reserve) and hand to `TaskHandlerManager`. On failure, send `kTaskRejected` (new TLV type; `TaskRejected { id, reason }`) and do **not** reserve.

- The gateway routes `kTaskRejected` by task id to the `ResultReceiver` (D14), so the HTTP client learns the task was not admitted (503-equivalent) instead of hanging.
- v1 does **not** auto-retry on rejection: in exact mode rejections are a rare safety net, and auto-retry semantics belong with the distributed-mode scheduler work.

### D12 — Pluggable scheduler

A new gateway extension category `Scheduler` (Registry pattern, like `NodeDiscoveryFactory`), configured via `GatewayConfig.scheduler` (ExtensionConfig). If unset, `round_robin` is used (behavior-compatible with today).

```cpp
class Scheduler {
 public:
  virtual auto RequiredProtocol() const -> std::string_view PURE;  // "push"
  virtual auto Choose(NodeDirectory& dir, const TaskOffer& offer) -> Node* PURE;
};
```

`TaskOffer` bundles the `Task` with its resolved `ResourceRequirements`. The directory exposes candidates filtered to connected nodes advertising `RequiredProtocol()`. v1 policies:

- `round_robin`: preserves `GetNextNode()` behavior (no requirements awareness).
- `capability_aware`: excludes nodes whose `shared_free` pools < requirements or whose per-type concurrency is exhausted, then picks the least-loaded (fewest free-slot ratio / lowest `in_flight`) among the eligible.

`GatewayHttpHandler` calls `scheduler->Choose(...)` instead of `GetNextNode()`; a `nullptr` return is a 503 to the client (same as no-node-available today).

### D13 — Function-requirement seam (deferred implementation)

The protocol must not hardcode "requirements come from task parameters". A small seam is introduced now; the repository-backed implementation is future work:

```cpp
// gateway side — resolves what a task needs before scheduling.
class RequirementsResolver {
 public:
  virtual auto Resolve(const FunctionRef& ref,
                       const google::protobuf::Map<std::string, std::string>& params)
      -> ResourceRequirements PURE;   // or absl::StatusOr
};
```

- `FunctionRef { type, id }`, with `id` taken from `parameters["function"]`.
- v1 implementation `ParamsOnlyRequirementsResolver`: reads resource entries from task parameters (e.g. `x-strij-resources-{pool}`); returns an empty map otherwise. Consistent with "body-embedded code has no requirements".
- The nodeagent-side evolution of the existing `FunctionResolver` (name→path) into a code-fetching resolver is out of scope; the seam is documented so the future repo work doesn't touch the probe/scheduler contracts.

### D14 — ResultReceiver error path

`ResultReceiver` gains a way to deliver a non-result outcome so `kTaskRejected` can reach the HTTP client (e.g. `DeliverError(std::string_view reason)`), and `HttpResultReceiver` maps it to an HTTP error status. This keeps result correlation by task id in `ResultReceiverStorage` unchanged.

### D15 — Configuration shape

- `GatewayConfig.scheduler` — `ExtensionConfig`; default `round_robin` when unset (backward compatible).
- `NodeAgentConfig` gains: `repeated ResourcePool pools`, `repeated PoolReservation reservations`, `repeated HandlerCapability handlers` (`{task_type, concurrency, function_sourced, default_resources?}`); the reserved `heartbeat_interval` (Duration) becomes the active state-snapshot cadence.

Pools and reservations are nodeagent-side config (the operator configures the node it runs on — distinct from the "no capabilities predefined in gateway config" requirement). `HandlerCapability` mirrors a configured task handler; the nodeagent derives its advertisement from config + handler manager. Auto-detection of hardware (nproc, `/proc/meminfo`, GPU discovery) is a future enhancement.

## Risks / Trade-offs

- [Snapshot-state broadcast is chatty at scale] → State frames are small; multicast replaces per-connection accounting complexity. A future non-heartbeat channel (Redis pub/sub) can deduplicate for many gateways.
- [Exact accounting is only exact for a single gateway] → `ExactStateTracker` is the v1 model; the dual-mode design (D10) explicitly reserves a cached model for multi-gateway. Rejections remain the universal safety net.
- [`kTaskRejected` could arrive after the gateway sent the task but the reservation is not yet observed] → In push mode there is no reservation: the nodeagent decides synchronously at `kTaskSubmission`; ordering on the single connection makes the reject/result ordering unambiguous.
- [A node advertised but unreachable is a wasted connect] → Normal discovery behavior; existing connect-failure path marks `kDisconnected`; membership still governed by the discovery source.
- [One advertisement per connection duplicates payloads] → Acceptable; frames are small and infrequent. Subscription negotiation is a future optimization.
- [The whole change is large] → It is delivered in four independently shippable phases (below), each with its own tests and each preserving existing behavior until replaced.

## Delivery Phases

1. **Dynamic NodeDirectory + identity** — `node_id`, `AddNode/RemoveNode/Reconcile`, snapshot reconciliation, `NodeInfo` grows. (D3, D4)
2. **Advertisement handshake** — `NodeCapabilities` proto, named pools/reservations/handlers, `kNodeAdvertisement` on connect, gateway stores/validates, `update_channels` + `scheduling_protocols` seams, nodeagent config fields. (D2, D5, D6, D7, D8, D15)
3. **State + admission** — `kNodeState` snapshots, `ExactStateTracker`, pool/concurrency admission, `kTaskRejected` + `ResultReceiver::DeliverError`. (D9, D10, D11, D14)
4. **Pluggable scheduler** — `Scheduler` extension family, `round_robin` + `capability_aware`, `GatewayConfig.scheduler`, `GatewayHttpHandler` wiring. (D12)

## Open Questions

- **Staleness/eviction for non-connection state channels**: when a future Redis/ZK channel exists, what TTL rule expires a node's state? (Connection-based liveness covers v1.)
- **`function_sourced` handler semantics**: how a handler that accepts function IDs declares itself, and how `default_resources` interacts with repo-resolved requirements when both exist.
- **Auto-detected vs configured pools**: whether v1 should auto-probe `cpu`/`mem` from the machine instead of requiring nodeagent config.
- **Reject retry policy**: whether any v1 scheduler policy should auto-retry a rejected task on another node (default: no).
