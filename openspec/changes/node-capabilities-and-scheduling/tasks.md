## 1. Dynamic NodeDirectory & identity

- [x] 1.1 Extend `NodeInfo` in `src/extensions/node_discovery/node_discovery.hh` with a `node_id` string; update `StaticNodeDiscovery` to derive `node_id` from the address (specs: node-discovery, dynamic-node-directory).
- [x] 1.2 Add a `node_id` member with accessors to `Node` in `src/core/gateway/node.hh`; pass it through construction from the discovery source.
- [x] 1.3 Rework `NodeDirectory` (`src/core/gateway/node_directory.{hh,cc}`) to own `Node`s keyed by `node_id` and to add `AddNode(node_id, address)`, `RemoveNode(node_id)`, and `Reconcile(snapshot)`; `AddNode` SHALL start connecting the `Node`. Keep `GetNextNode()`, and the count accessors (specs: dynamic-node-directory, node-directory).
- [x] 1.4 Wire the discovery callback in `src/exe/gateway/gateway.cc` to `NodeDirectory::Reconcile` (remove the local `node_addresses` vector capture); construct the directory with no initial nodes.
- [x] 1.5 Add `test/core/gateway/node_directory_test.cc`: runtime add/remove/reconcile against a fake dynamic discovery, unchanged-snapshot no-op, and removal closing the connection (specs: dynamic-node-directory).
- [x] 1.6 Add/extend `StaticNodeDiscovery` tests for address-derived `node_id` (specs: node-discovery).

## 2. Advertisement & node plane

- [x] 2.1 Add `api/core/node/capabilities.proto` (package `strij.node`) with `NodeCapabilities`, `ResourcePool`, `PoolReservation`, `HandlerCapability`, `UpdateChannel`, `SchedulingProtocol`, `ResourceRequirements`, `FunctionRef`, `NodeState`, `PoolUsage`, `TypeUsage`; add a `strij_cc_library`/proto target (specs: node-advertisement, node-state-reporting).
- [x] 2.2 Add `kNodeAdvertisement = 3`, `kNodeState = 4`, `kTaskRejected = 5` constants to `TlvFrame` in `src/core/io/tlv_frame.hh` (specs: typed-tlv-messages).
- [x] 2.3 Add `repeated ResourcePool pools` and `repeated PoolReservation reservations` to `NodeAgentConfig` and activate the reserved `heartbeat_interval` (Duration) in `api/core/config/nodeagent.proto`. Handler capacity lives in each task handler extension config as a shared `HandlerCapacity` message (specs: nodeagent-config).
- [x] 2.4 Nodeagent: build the `NodeCapabilities` advertisement from config + `TaskHandlerManager`, and send `kNodeAdvertisement` as the first frame on every established connection (specs: node-advertisement).
- [x] 2.5 Nodeagent startup validation: fail on an empty `pools` list, on a `PoolReservation` referencing an undeclared pool, or on a `HandlerCapability` naming an unregistered task type (specs: nodeagent-config).
- [x] 2.6 Gateway: handle `kNodeAdvertisement` in `GatewayTlvHandler` — store capabilities on the owning `Node`, warn on `capability_version` mismatch, and rekey the directory to the advertised `node_id` (specs: node-advertisement, dynamic-node-directory).
- [x] 2.7 Add `GetCapabilities()`/`GetState()` accessors on `Node`; expose a protocol-filtered candidate iteration on `NodeDirectory` (specs: node-directory).
- [x] 2.8 Add the `RequirementsResolver` interface and `ParamsOnlyRequirementsResolver` implementation (reads resource entries from task parameters; empty otherwise) (specs: node-advertisement).
- [x] 2.9 Tests: capabilities proto round-trip; nodeagent advertisement handshake on connect; gateway advertisement storage + rekey; nodeagent config validation (specs: node-advertisement, nodeagent-config).

## 3. State reporting & admission

- [x] 3.1 Add `TaskRejected { string id; string reason; }` to `api/core/task/task.proto` (specs: task-protocol).
- [x] 3.2 Nodeagent admission: an `AdmissionController` tracking per-pool in-use and per-type in-flight counts; check `shared_free(pool) ≥ requirement` and concurrency headroom on `kTaskSubmission`; reserve on admit, release on completion, send `kTaskRejected` on failure (specs: node-state-reporting).
- [x] 3.3 Nodeagent state reporter: send `kNodeState` snapshots (with `node_id`, `seq`, `timestamp`, pool usage, in-flight, per-type usage) on every established connection at `heartbeat_interval` (specs: node-state-reporting).
- [x] 3.4 Gateway: `ExactStateTracker` incrementing on task send, decrementing on final result/rejection, and applying `kNodeState` snapshots as a correction signal (specs: node-state-reporting).
- [x] 3.5 Gateway: handle `kNodeState` (update the owning node's state) and `kTaskRejected` (route by task id to the result receiver) in `GatewayTlvHandler` (specs: node-state-reporting).
- [x] 3.6 Add `ResultReceiver::DeliverError(reason)` and map it to an HTTP error response in `HttpResultReceiver` (specs: node-state-reporting).
- [x] 3.7 Tests: admission accept/reject; state snapshot framing and cadence; exact tracker increment/decrement; rejection delivery to the HTTP client; no v1 auto-retry (specs: node-state-reporting).

## 4. Pluggable scheduler

- [x] 4.1 Define the `Scheduler` interface (`RequiredProtocol()`, `Choose(NodeDirectory&, const TaskOffer&)`) and `TaskOffer` (task + resolved `ResourceRequirements`); add `Registry<SchedulerFactory>` category (specs: pluggable-scheduler).
- [x] 4.2 Implement the `round_robin` scheduler preserving current `GetNextNode()` behavior (specs: pluggable-scheduler).
- [x] 4.3 Implement the `capability_aware` scheduler: exclude nodes with insufficient shared-free pools or exhausted per-type concurrency; pick the least-loaded eligible node (specs: pluggable-scheduler).
- [x] 4.4 Add `ExtensionConfig scheduler` to `GatewayConfig`; in `gateway.cc` fail to start when `scheduler` is unset or names an unregistered scheduler (specs: gateway-config, pluggable-scheduler).
- [x] 4.5 Switch `GatewayHttpHandler` from `GetNextNode()` to the configured scheduler's `Choose`, mapping `nullptr` to the no-node-available error status (specs: pluggable-scheduler).
- [x] 4.6 Tests: round_robin rotation; capability_aware filtering and least-loaded choice; protocol-filtered candidate exclusion; gateway config missing/unknown-name handling; HTTP routing through the scheduler (specs: pluggable-scheduler).

## 5. Integration & verification

- [ ] 5.1 Update `gateway.yaml` / `nodeagent.yaml` examples with `scheduler`, `pools`, `reservations`, `heartbeat_interval`, and `capacity` inside the task handler extension configs.
- [ ] 5.2 End-to-end: one gateway (static discovery) + two nodeagents running `echo` with configured pools; verify routing, state snapshots, and rejection when a pool is exhausted.
- [ ] 5.3 Run `make build`, `make test`, `make test_asan`, and `make clang-tidy`; fix findings.
